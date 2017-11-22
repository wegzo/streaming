#include "request_packet.h"
#include "assert.h"

bool request_packet::get_clock(presentation_clock_t& clock) const
{
    if(!this->topology)
        return false;

    clock = this->topology->get_clock();
    return !!clock;
}

request_queue::request_queue(int first_packet_number) :
    first_packet_number(first_packet_number), last_packet_number(first_packet_number)
{
}

int request_queue::get_index(int packet_number) const
{
    return packet_number - this->first_packet_number;
}

void request_queue::push(const request_t& request)
{
    scoped_lock lock(this->requests_mutex);
    if(request.rp.packet_number < this->first_packet_number)
    {
        assert_(false);
        /*const int diff = this->first_packet_number - request.rp.packet_number;
        this->requests.insert(this->requests.begin(), diff, request);
        this->first_packet_number = request.rp.packet_number;*/
    }
    else if(request.rp.packet_number > this->last_packet_number)
    {
        const int diff = request.rp.packet_number - this->last_packet_number;
        this->requests.insert(this->requests.end(), diff, request);
        this->last_packet_number = request.rp.packet_number;
    }
    else if(request.rp.packet_number == this->first_packet_number &&
        request.rp.packet_number == this->last_packet_number)
    {
        assert_(this->requests.empty());
        this->requests.push_back(request);
    }
    else
        this->requests[this->get_index(request.rp.packet_number)] = request;
}

bool request_queue::pop(request_t& request)
{
    scoped_lock lock(this->requests_mutex);
    if(this->get(request))
    {
        this->requests.pop_front();

        this->first_packet_number++;
        /*if(this->first_packet_number > this->last_packet_number)
        {
            assert_(this->requests.empty());
            this->last_packet_number = this->first_packet_number;
        }*/

        return true;
    }

    return false;
}

bool request_queue::get(request_t& request)
{
    scoped_lock lock(this->requests_mutex);
    if(!this->requests.empty())
    {
        if(this->first_packet_number == this->requests.front().rp.packet_number)
        {
            request = this->requests.front();
            return true;
        }
    }

    return false;
}
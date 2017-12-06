#pragma once
#include "media_source.h"
#include "media_stream.h"
#include <memory>

// null source is used to enable topologies that do nothing;
// it simplifies the creation of topologies

class source_null : public media_source
{
public:
    explicit source_null(const media_session_t& session);

    media_stream_t create_stream();
};

typedef std::shared_ptr<source_null> source_null_t;

class stream_null : public media_stream
{
private:
    source_null_t source;
public:
    explicit stream_null(const source_null_t& source);

    bool get_clock(presentation_clock_t& c) {return this->source->session->get_current_clock(c);}

    result_t request_sample(request_packet&, const media_stream*);
    result_t process_sample(const media_sample_view_t&, request_packet&, const media_stream*);
};
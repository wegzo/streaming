#include "control_class.h"
#include "control_pipeline2.h"

control_class::control_class(control_set_t& active_controls, std::recursive_mutex& mutex) :
    mutex(mutex),
    parent(NULL), disabled(false),
    active_controls(active_controls)
{
}

void control_class::build_and_switch_topology()
{
    this->get_root()->build_and_switch_topology();
}

void control_class::activate()
{
    control_class* root = this->get_root();
    control_set_t new_set;
    root->activate(this->active_controls, new_set);
    this->active_controls = std::move(new_set);

    this->build_and_switch_topology();
}

void control_class::deactivate()
{
    this->disabled = true;
    this->activate();
    this->disabled = false;
}

void control_class::disable()
{
    this->disabled = true;
    this->activate();
}

control_class* control_class::get_root()
{
    if(this->parent)
        return this->parent->get_root();
    else
        return this;
}
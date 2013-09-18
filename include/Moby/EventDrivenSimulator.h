/****************************************************************************
 * Copyright 2011 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#ifndef _EVENT_SIMULATOR_H
#define _EVENT_SIMULATOR_H

#include <map>
#include <Moby/sorted_pair>
#include <Moby/Simulator.h>
#include <Moby/ImpactEventHandler.h>
#include <Moby/Event.h>

namespace Moby {

class ContactParameters;
class CollisionDetection;
class CollisionGeometry;

/// An event-driven simulator
class EventDrivenSimulator : public Simulator
{
  friend class CollisionDetection;
  friend class DeformableCCD;

  private:
/*
    // interval type for generalized velocity
    class Interval
    {
      public:
        enum IntervalType { eBinary, eTernary, eQuaternary };
        double get_position(double t);
        double get_velocity(double t);

        // what type of interval is this?
        IntervalType type;

        // beginning/end of interval coordinates 
        double q0, qf;

        // beginning/end of interval velocities
        double qd0, qdf;

        // midpoint for ternary interval
        double qc;

        // constant for ternary and quaternary intervals
        double tc;

        // constants for quaternary intervals
        double ta, tb, k1, k2, qa, qb;

      private:
        void calc_ternary_interval();
        void calc_quaternary_interval();
    };

    class CoordIterator
    {
      friend class PhaseSequence;

      public:
        CoordIterator() { _counter = 0; _total = 0; }
        bool has_next() const { return _counter < _total; }
        void get_next(double& t0, double& tf, std::vector<Ravelin::VectorNd>& q0, std::vector<Ravelin::VectorNd>& qf);

      private:
        unsigned _counter; 
    };      

    // class for storing a sequence of generalized coordinates and velocities
    class PhaseSequence
    {
      public:
 
        /// sequence of bodies
        std::vector<DynamicBodyPtr> bodies;

        /// sequence of generalized coordinates, given per body
        std::vector<std::vector<Ravelin::VectorNd> > q;

        /// sequence of velocity intervals
        std::vector<std::vector<Interval> > qd;

        /// gets the generalized velocities at the given t
        void get_velocities(std::vector<std::pair<DynamicBodyPtr, Ravelin::VectorNd> >& velocities) const;

        /// gets the iterator over the coordinates
        CoordIterator iterator() const;    
    };
*/
    // class for comparing two events for purposes of setting event tolerances
    class EventCompare
    {
      public: bool operator()(const Event& a, const Event& b)
      {
        // check for event type disparity
        if (a.event_type != b.event_type)
          return a.event_type < b.event_type;

        // event types are the same - now each event type must be processed
        // separately
        if (a.event_type == Event::eContact)
        {
          // see whether the bodies are the same
          return make_sorted_pair(a.contact_geom1->get_single_body(), a.contact_geom2->get_single_body()) < make_sorted_pair(b.contact_geom1->get_single_body(), b.contact_geom2->get_single_body());
        }
        else if (a.event_type == Event::eLimit)
        {
          // check whether the joints are the same
          if (a.limit_joint != b.limit_joint)
            return a.limit_joint < b.limit_joint;

          // check whether the limits are the same
          if (a.limit_upper != b.limit_upper)
            return a.limit_upper < b.limit_upper;

          // finally, check whether the DOFs are the same
          return a.limit_dof < b.limit_dof;
        }
        else // event is a constraint event
        {
          return a.constraint_joint < b.constraint_joint; 
        }  
      }
    };

  public:
    EventDrivenSimulator();
    virtual ~EventDrivenSimulator() {}
    virtual void load_from_xml(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    virtual void save_to_xml(XMLTreePtr node, std::list<boost::shared_ptr<const Base> >& shared_objects) const;
    virtual void output_object_state(std::ostream& out) const;
    virtual double step(double dt);
    void get_coords(std::vector<Ravelin::VectorNd>& q) const;
    void get_velocities(std::vector<Ravelin::VectorNd>& q) const;

    /// The phase sequence
//    PhaseSequence _phases;

    /// The coordinates vector before and after the step
    std::vector<Ravelin::VectorNd> _q0, _qf;

    /// The velocities vector before and after the step
    std::vector<Ravelin::VectorNd> _qd0, _qdf;

    /// Vectors set and passed to collision detection
    std::vector<std::pair<DynamicBodyPtr, Ravelin::VectorNd> > _x0, _x1;

    /// Gets the shared pointer for this
    boost::shared_ptr<EventDrivenSimulator> get_this() { return boost::dynamic_pointer_cast<EventDrivenSimulator>(shared_from_this()); }
    
   /// The collision detection mechanisms
    std::list<boost::shared_ptr<CollisionDetection> > collision_detectors;

    /// Callback function after a mini-step is completed
    void (*post_mini_step_callback_fn)(EventDrivenSimulator* s);

    /// The callback function (called when events have been determined)
    /**
     * The callback function can remove events from the list, which will disable
     * their processing (however, doing so may prevent the simulation from
     * making progress, as the simulator attempts to disallow violations.
     */
    void (*event_callback_fn)(std::vector<Event>&, boost::shared_ptr<void>);

    /// The callback function (called after event impulses are applied)
    void (*event_post_impulse_callback_fn)(const std::vector<Event>&, boost::shared_ptr<void>);

    /// Data passed to event callback
    boost::shared_ptr<void> event_callback_data;
    
    /// Data passed to event impulse callback
    boost::shared_ptr<void> event_post_impulse_callback_data;
 
    /// Gets the (sorted) event data
    std::vector<Event>& get_events() { return _events; }

    /// Mapping from objects to contact parameters
    std::map<sorted_pair<BasePtr>, boost::shared_ptr<ContactParameters> > contact_params;

    /// If set to 'true' event driven simulator will process contact points for rendering
    bool render_contact_points;

    /// User time spent by collision detection on the last step
    double coldet_utime;

    /// System time spent by collision detection on the last step
    double coldet_stime;

    /// User time spent by event handling on the last step
    double event_utime;

    /// System time spent by event handling on the last step
    double event_stime;

    /// The relative error tolerance for adaptive Euler stepping
    double rel_err_tol;

    /// The absolute error tolerance for adaptive Euler stepping
    double abs_err_tol;

  private:
    void integrate_si_Euler(double dt);
    static void determine_treated_bodies(std::list<std::list<Event*> >& groups, std::vector<DynamicBodyPtr>& bodies);
    double find_events(double dt);
    double find_next_event_time() const;
    double find_and_handle_si_events(double dt);
    void preprocess_event(Event& e);
    void check_violation();
    void find_limit_events(double dt, std::vector<Event>& limit_events);
    double find_TOI(double dt); 
    void handle_events();
    boost::shared_ptr<ContactParameters> get_contact_parameters(CollisionGeometryPtr geom1, CollisionGeometryPtr geom2) const;
    bool has_active_acceleration_events() const;
    bool has_active_velocity_events() const;
    bool solve_acceleration_events();
    void step_adaptive_si_Euler(double dt);
    void step_si_Euler(double dt);
    void set_coords(double t);
    void set_velocities(double t);
    void set_coords(const std::vector<Ravelin::VectorNd>& q) const;
    void set_velocities(const std::vector<Ravelin::VectorNd>& qd) const;

    // Visualization functions
    void visualize_contact( Event& event );

    /// Determines whether the simulation constraints have been violated
    bool _simulation_violated;

    /// Work vector
    Ravelin::VectorNd _workV;

    /// The vector of events
    std::vector<Event> _events;

    /// Event tolerances
    std::map<Event, double, EventCompare> _event_tolerances;

    /// Object for handling impact events
    ImpactEventHandler _impact_event_handler;
}; // end class

} // end namespace

#endif



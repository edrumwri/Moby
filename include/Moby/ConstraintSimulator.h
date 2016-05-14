/****************************************************************************
 * Copyright 2015 Evan Drumwright
 * This library is distributed under the terms of the Apache V2.0 
 * License (obtainable from http://www.apache.org/licenses/LICENSE-2.0).
 ****************************************************************************/

#ifndef _CONSTRAINT_SIMULATOR_H
#define _CONSTRAINT_SIMULATOR_H

#include <map>
#include <Ravelin/sorted_pair>
#include <Moby/Simulator.h>
#include <Moby/ImpactConstraintHandler.h>
#include <Moby/PairwiseDistInfo.h>
#include <Moby/CCD.h>
#include <Moby/Constraint.h>
#include <Moby/ConstraintStabilization.h>

namespace Moby {

class Dissipation;
class ContactParameters;
class CollisionDetection;
class CollisionGeometry;

/// An virtual class for simulation with constraints 
class ConstraintSimulator : public Simulator
{
  friend class CollisionDetection;
  friend class ConstraintStabilization;

  public:
    ConstraintSimulator();
    virtual void load_from_xml(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    virtual void save_to_xml(XMLTreePtr node, std::list<boost::shared_ptr<const Base> >& shared_objects) const;
    boost::shared_ptr<ContactParameters> get_contact_parameters(CollisionGeometryPtr geom1, CollisionGeometryPtr geom2) const;
    const std::vector<PairwiseDistInfo>& get_pairwise_distances() const { return _pairwise_distances; }

    /// The constraint stabilization mechanism
    ConstraintStabilization cstab;

    /// Determines whether two geometries are not checked
    std::set<Ravelin::sorted_pair<CollisionGeometryPtr> > unchecked_pairs;

    /// Vectors set and passed to collision detection
    std::vector<std::pair<ControlledBodyPtr, Ravelin::VectorNd> > _x0, _x1;

    /// Callback function for getting contact parameters
    boost::shared_ptr<ContactParameters> (*get_contact_parameters_callback_fn)(CollisionGeometryPtr g1, CollisionGeometryPtr g2);

    /// Callback function after a mini-step is completed
    void (*post_mini_step_callback_fn)(ConstraintSimulator* s);

    /// The callback function (called when constraints have been determined)
    /**
     * The callback function can remove constraints from the list, which will disable
     * their processing (however, doing so may prevent the simulation from
     * making progress, as the simulator attempts to disallow violations.
     */
    void (*constraint_callback_fn)(std::vector<Constraint>&, boost::shared_ptr<void>);

    /// The callback function (called after forces/impulses are applied)
    void (*constraint_post_callback_fn)(const std::vector<Constraint>&, boost::shared_ptr<void>);

    /// Data passed to unilateral constraint callback
    boost::shared_ptr<void> constraint_callback_data;
    
    /// Data passed to post-constraint callback
    boost::shared_ptr<void> constraint_post_callback_data;
 
    /// Gets the (sorted) rigid constraint data
    std::vector<Constraint>& get_rigid_constraints() { return _rigid_constraints; }

    /// Mapping from objects to contact parameters
    std::map<Ravelin::sorted_pair<BasePtr>, boost::shared_ptr<ContactParameters> > contact_params;

    /// If set to 'true' simulator will process contact points for rendering
    bool render_contact_points;

    /// Gets the collision detection mechanism
    boost::shared_ptr<CollisionDetection> get_collision_detection() const { return _coldet; }

  protected:
    void calc_impacting_unilateral_constraint_forces(double dt);
    void find_unilateral_constraints();
    void preprocess_constraint(Constraint& e);
    void determine_geometries();
    void broad_phase(double dt);
    void calc_pairwise_distances();
    void visualize_contact( Constraint& constraint );

    /// Object for handling impact constraints
    ImpactConstraintHandler _impact_constraint_handler;

    /// The vector of rigid constraints
    std::vector<Constraint> _rigid_constraints;

  protected:

    double calc_CA_step();
    double calc_next_CA_Euler_step(double contact_dist_thresh) const;

    /// Pairwise distances at bodies' current configurations
    std::vector<PairwiseDistInfo> _pairwise_distances;

    /// Work vector
    Ravelin::VectorNd _workV;

    /// The collision detection mechanism
    boost::shared_ptr<CollisionDetection> _coldet;

    /// The geometries in the simulator
    std::vector<CollisionGeometryPtr> _geometries;

    /// Geometric pairs that should be checked for unilateral constraints (according to broad phase collision detection)
    std::vector<std::pair<CollisionGeometryPtr, CollisionGeometryPtr> > _pairs_to_check;
}; // end class

} // end namespace

#endif



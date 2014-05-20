/****************************************************************************
 * Copyright 2014 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#ifndef _SDF_READER_H
#define _SDF_READER_H

#include <Ravelin/VectorNd.h>
#include <Ravelin/MatrixNd.h>
#include <map>
#include <string>
#include <vector>
#include <boost/shared_ptr.hpp>
#include <Moby/Types.h>

namespace Moby {

class Simulator;
class RigidBody;
class RCArticulatedBody;
class MCArticulatedBody;
class Primitive;

/// Used to read the simulator state from XML
class SDFReader
{
  public:
    static std::map<std::string, BasePtr> read(const std::string& fname);
    
  private:
    enum TupleType { eNone, eVectorN, eVector3, eQuat };
    static boost::shared_ptr<const XMLTree> find_subtree(boost::shared_ptr<const XMLTree> root, const std::string& name);
    static std::list<boost::shared_ptr<const XMLTree> > find_tag(const std::string& tag, boost::shared_ptr<const XMLTree> root);
    static void find_tag(const std::string& tag, boost::shared_ptr<const XMLTree> root, std::list<boost::shared_ptr<const XMLTree> >& l);
    static void process_tag(const std::string& tag, boost::shared_ptr<const XMLTree> root, void (*fn)(boost::shared_ptr<const XMLTree>, std::map<std::string, BasePtr>&), std::map<std::string, BasePtr>& id_map);
    static PrimitivePtr read_heightmap(boost::shared_ptr<const XMLTree> node);
    static PrimitivePtr read_plane(boost::shared_ptr<const XMLTree> node);
    static PrimitivePtr read_box(boost::shared_ptr<const XMLTree> node);
    static PrimitivePtr read_sphere(boost::shared_ptr<const XMLTree> node);
    static PrimitivePtr read_cylinder(boost::shared_ptr<const XMLTree> node);
    static PrimitivePtr read_cone(boost::shared_ptr<const XMLTree> node);
    static PrimitivePtr read_trimesh(boost::shared_ptr<const XMLTree> node);
    static void read_event_driven_simulator(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_simulator(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static RigidBodyPtr read_rigid_body(boost::shared_ptr<const XMLTree> node);
    static void read_mc_abody(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_rc_abody(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_rc_abody_symbolic(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_osg_group(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_collision_node(boost::shared_ptr<const XMLTree> node, RigidBodyPtr rb);
    static PrimitivePtr read_geometry(boost::shared_ptr<const XMLTree> node);
    static Ravelin::Pose3d read_pose(boost::shared_ptr<const XMLTree> node);
    static Ravelin::SpatialRBInertiad read_inertia(boost::shared_ptr<const XMLTree> node, RigidBodyPtr rb);

    static void read_joint_plugin(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_prismatic_joint(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_fixed_joint(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_revolute_joint(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_spherical_joint(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_universal_joint(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_gravity_force(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_damping_force(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_stokes_drag_force(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_odepack_integrator(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_bulirsch_stoer_integrator(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_rk4i_integrator(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_rk4_integrator(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_rkf4_integrator(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_variable_euler_integrator(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static void read_euler_integrator(boost::shared_ptr<const XMLTree> node, std::map<std::string, BasePtr>& id_map);
    static TupleType get_tuple(boost::shared_ptr<const XMLTree> node);
}; // end class
} // end namespace

#endif


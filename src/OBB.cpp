/****************************************************************************
 * Copyright 2008 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#include <boost/foreach.hpp>
#include <boost/algorithm/minmax.hpp>
#include <boost/numeric/interval/io.hpp>
#include <Moby/RigidBody.h>
#include <Moby/XMLTree.h>
#include <Moby/Log.h>
#include <Moby/ThickTriangle.h>
#include <Moby/OBB.h>

using namespace Ravelin;
using namespace Moby;
using boost::numeric::interval;
using boost::shared_ptr;
using boost::static_pointer_cast;
using boost::dynamic_pointer_cast;
using boost::const_pointer_cast;
using std::cerr;
using std::endl;
using std::pair;
using std::list;

OBB::OBB()
{
  R = Matrix3d::zero();
  center.set_zero();
  l.set_zero();
}

/// Copies an OBB
/**
 * \note userdata and node info (children) are not copied
 */
void OBB::operator=(const OBB& obb)
{
  this->R = obb.R;
  this->center = obb.center;
  this->l = obb.l;
}

/// Constructs an OBB with given center, principle axes and lengths of principle axes
OBB::OBB(const Point3d& center, const Matrix3d& R, const Vector3d& l) 
{
  this->center = center;
  this->l = l;
  this->R = R;
}

/// Constructs a OBB expanded by the given vector
/**
 * \param o the original bounding box
 * \param v the vector to expand the box by
 */
OBB::OBB(const OBB& o, const Vector3d& v)
{
  const unsigned X = 0, Y = 1, Z = 2;

  // if the vector is essentially zero, just set this to o 
  if (v.norm_sq() < std::numeric_limits<double>::epsilon())
  {
    *this = o;
    return;
  }

  // get the axes of o
  Vector3d axis1, axis2, axis3;
  o.R.get_column(X, axis1);
  o.R.get_column(Y, axis2);
  o.R.get_column(Z, axis3);

  // get all vertices of the OBB
  list<Point3d> verts;
  o.get_vertices(std::back_inserter(verts));

  // add expanded vertices
  list<Point3d>::const_iterator vi = verts.begin();
  for (unsigned i=0; i< 8; i++)
    verts.push_back(*vi++ + v);

  // get expanded center
  center = o.center + v*0.5;

  // compute the lengths of the expanded bounding box using the current directions
  double l1[3];
  calc_lengths(axis1, axis2, axis3, center, verts.begin(), verts.end(), l1);

  // get the length of v
  double vlen = v.norm();

  // if length of v less than largest length, use original axes and new lengths
  if (vlen < std::max(l1[0], std::max(l1[1], l1[2])))
  {
    R = o.R;
    l[X] = l1[X];
    l[Y] = l1[Y];
    l[Z] = l1[Z];
    return;  
  }

  // get the direction of v
  Vector3d vdir = v/vlen;

  // determine the vector that results in the minimum bounding volume given
  // that one direction is v
  Vector3d vmin;
  align(verts.begin(), verts.end(), vdir, vmin);

  // determine the third vector
  Vector3d v3 = Vector3d::cross(vdir, vmin);
  v3.normalize();

  // compute the lengths
  double l2[3];
  calc_lengths(vdir, vmin, v3, center, verts.begin(), verts.end(), l2);

  // see which bounding box to use
  if (l1[0]*l1[1]*l1[2] <= l2[0]*l2[1]*l2[2])
  {
    // use original axes; set new lengths
    R = o.R;
    l[X] = l1[X];
    l[Y] = l1[Y];
    l[Z] = l1[Z];
  }
  else
  {
    // use new axes
    R.set_column(X, vdir);
    R.set_column(Y, vmin);
    R.set_column(Z, v3);

    // set lengths
    l[X] = l2[X];
    l[Y] = l2[Y];
    l[Z] = l2[Z];
  }
}

/// Constructs an OBB from a triangle mesh using the mesh inertia method [Ericson, 2003]
/**
 * Requires a O(n) operation to compute the inertial properties of the triangle mesh
 */
/*
OBB::OBB(TriangleMeshPrimitive& trimesh)
{
  const unsigned X = 0, Y = 1, Z = 2;

  // store old mesh mass 
  double mass = trimesh.get_mass();
  trimesh.set_density(1.0);

  // determine center and principle axes
  this->center = trimesh.get_com();
  this->R = trimesh.get_inertia();
  Vector3 axis1, axis2, axis3;
  R.get_column(X, axis1.begin());
  R.get_column(Y, axis2.begin());
  R.get_column(Z, axis3.begin());
  axis1.normalize();
  axis2.normalize();
  axis3.normalize();

  // compute the lengths
  this->l.set_zero();
  const std::vector<Vector3>& verts = trimesh.get_mesh().get_vertices();
  for (unsigned i=0; i< verts.size(); i++)
  {
    Vector3 v = verts[i] - center;
    l[0] = std::max(l[0], std::fabs(axis1.dot(v)));
    l[1] = std::max(l[1], std::fabs(axis2.dot(v)));
    l[2] = std::max(l[2], std::fabs(axis3.dot(v)));
  }

  // restore old mesh mass
  trimesh.set_mass(mass);
}
*/

/// Calculates the size (number of elements in) of an OBB tree
unsigned OBB::calc_size() const
{
  unsigned sz = 1;
  BOOST_FOREACH(BVPtr child, children)
    sz += dynamic_pointer_cast<OBB>(child)->calc_size();

  return sz;
}

/// Computes the squared distance from a point to an OBB
double OBB::calc_sq_dist(const OBB& o, const Point3d& p)
{
  const unsigned THREE_D = 3;

  // transform the point to OBB coordinates
  Point3d point = o.R.transpose_mult(p - o.center);

  // compute the distance
  double sq_dist = 0.0;
  for (unsigned i=0; i< THREE_D; i++)
  {
    if (point[i] < -o.l[i] || point[i] > o.l[i])
      sq_dist += (o.l[i] - point[i]) * (o.l[i] - point[i]);
    else
      return 0.0;
  }

  return sq_dist;
}

/// Determines whether a point is outside a OBB to within the given tolerance
bool OBB::outside(const OBB& a, const Point3d& p, double tol)
{
  // transform the point to OBB coordinates
  Point3d point = a.R.transpose_mult(p - a.center);

  // OBB is now effectively a centered AABB; check whether point is 
  // outside of the AABB
  for (unsigned i=0; i< 3; i++)
  {
    if (!CompGeom::rel_equal(point[i], -a.l[i], tol) && point[i] < -a.l[i])
      return true;
    if (!CompGeom::rel_equal(point[i], a.l[i], tol) && point[i] > a.l[i])
      return true;
  }

  // must be inside or on (to within tolerance)
  return false;
}

/// Determines whether an OBB and a line/ray/line segment intersect
/**
 * When intersecting, return intersection distance tmin and point q of
 * intersection.
 * \param a the OBB to check for intersection
 * \param seg the line segment to check for intersection
 * \param tmin on entry, contains the minimum value of the line parameter- for
 *        a line segment, this will generally be 0; when an intersection
 *        occurs, this will contain the distance of intersection from the
 *        tmin that was input on return
 * \param tmax the maximum value of the line parameter- for a line segment,
 *        this will generally be 1
 * \param q contains the point of intersection, if any, on return
 * \return <b>true</b> if the OBB and line intersect, <b>false</b> otherwise
 * \note code adapted from [Ericson, 2005], pp. 180-181
 */
bool OBB::intersects(const OBB& a, const LineSeg3& seg, double& tmin, double tmax, Point3d& q)
{
  // compute the inverse of the OBB transform
  Pose3d T(Quatd(a.R), a.center);
  T.invert();

  // convert the line segment to OBB space
  Point3d p = T.transform(seg.first);
  Vector3d d = T.transform(seg.second) - p;

  FILE_LOG(LOG_BV) << "OBB::intersects() entered" << endl; 
  FILE_LOG(LOG_BV) << "  -- checking intersection between line segment " << seg.first << " / " << seg.second << " and OBB: " << endl << a;

  // for all three slabs
  for (unsigned i=0; i< 3; i++)
  {
    if (std::fabs(d[i]) < NEAR_ZERO)
    {
      // line is parallel to slab; no hit if origin not within slab
      if (p[i] < -a.l[i] || p[i] > a.l[i])
      {
        FILE_LOG(LOG_BV) << "  -- seg parallel to slab " << i << " and origin not w/in slab = no intersection" << endl; 
        FILE_LOG(LOG_BV) << "    amin: " << -a.l << "  amax: " << a.l << "  p: " << p << endl;
        return false;
      }
    }
    else
    {
      // compute intersection value of line with near and far plane of slab
      double ood = 1.0 / d[i];
      double t1 = (-a.l[i] - p[i]) * ood;
      double t2 = (a.l[i] - p[i]) * ood;

      FILE_LOG(LOG_BV) << "  ood: " << ood << " t1: " << t1 << "  t2: " << t2 << endl; 

      // make t1 be the intersection with the near plane, t2 with far plane
      if (t1 > t2)
        std::swap(t1, t2);

      // compute the intersection of slab intersection intervals
      tmin = std::max(tmin, t1);
      tmax = std::min(tmax, t2);

      // exit with no collision as soon as slab intersection becomes empty
      if (tmin > tmax + NEAR_ZERO)
      {
        FILE_LOG(LOG_BV) << "  tmin (" << tmin << ") > tmax (" << tmax << ") -- seg and OBB do not intersect" << endl;

        return false;
      }
    }
  }

  // ray intersects all three slabs; return point q and intersection t value
  q = a.center + a.R * (p + d * tmin);

  FILE_LOG(LOG_BV) << "OBB::intersects() - seg and OBB intersect; first intersection: " << tmin << "(" << q << ")" << endl; 
  FILE_LOG(LOG_BV) << "OBB::intersects() exiting" << endl; 

  return true;
}

/// Determines the distance between two OBBs
double OBB::calc_dist(const OBB& a, const OBB& b, Point3d& cpa, Point3d& cpb)
{
  // need to implement this!
  throw std::runtime_error("OBB::calc_dist() unimplemented!");

  return 0.0;
}

/// Determines the distance between two OBBs
/**
 * \param a the first OBB
 * \param b the second OBB
 * \param aTb the relative transform from b to a
 * \param cpa the closest point on a to b, on return (in a's frame) 
 * \param cpb the closest point on b to a, on return (in a's frame)
 * \return the distance between a and b
 */
double OBB::calc_dist(const OBB& a, const OBB& b, const pair<Quatd, Origin3d>& aTb, Point3d& cpa, Point3d& cpb)
{
  // copy b
  OBB bcopy(b);

  // transform the center and orientation of b
  bcopy.center = aTb.first * b.center + aTb.second;
  Matrix3d R = aTb.first;
  bcopy.R = R * b.R;

  // perform the distance query
  return calc_dist(a, bcopy, cpa, cpb);
}

/// Determines whether two OBBs intersect
/**
 * \param aTb the relative transform from b to a
 */
bool OBB::intersects(const OBB& a, const OBB& b, const pair<Quatd, Origin3d>& aTb)
{
  // copy b
  OBB bcopy(b);

  // transform the center and orientation of b
  bcopy.center = aTb.first * b.center + aTb.second;
  Matrix3d R = aTb.first;
  bcopy.R = R * b.R;

  // perform the intersection
  return intersects(a, bcopy);
}

/// Determines whether two OBBs intersect another
/**
 * Code adapted from [Ericson, 2005]
 */
bool OBB::intersects(const OBB& a, const OBB& b)
{
  const unsigned THREE_D = 3, X = 0, Y = 1, Z = 2;
  enum Dims { eDim0 = 0, eDim1 = 1, eDim2 = 2 };
  double ra, rb;

  FILE_LOG(LOG_BV) << "checking OBBs for intersection" << endl;
  FILE_LOG(LOG_BV) << " OBB 1: " << endl << a;
  FILE_LOG(LOG_BV) << " OBB 2: " << endl << b;

  // transpose of A's matrix will convert to A's coordinate frame
  // Compute rotation matrix expressing b in a's coordinate frame
  const Matrix3d Rab = a.R.transpose_mult(b.R);
  
  // compute translation vector t in coordinate frame of a
  Vector3d t = a.R.transpose_mult(b.center - a.center);

  // compute common subexpressions; add in an epsilon term to counteract
  // arithmetic errors when two edges are parallel and their cross product
  // is near zero
  Matrix3d abs_Rab = Rab;
  for (unsigned i=0; i< THREE_D; i++)
    for (unsigned j=0; j< THREE_D; j++)
      abs_Rab(i,j) = std::fabs(Rab(i,j)) + NEAR_ZERO;

  // test axes L = A0, L = A1, L = A2
  for (unsigned i=0; i< THREE_D; i++)
  {
    ra = a.l[i];
    rb = b.l[eDim0] * abs_Rab(i,0) + b.l[eDim1]*abs_Rab(i,1) + b.l[eDim2]*abs_Rab(i,2);
    if (std::fabs(t[i]) > ra + rb)
    {
      FILE_LOG(LOG_BV) << "OBBs do not intersect" << endl;
      return false;
    }
  }

  // test axes L = B0, L = B1, L = B2
  Vector3d Rab_col;
  for (unsigned i=0; i< THREE_D; i++)
  {
    ra = a.l[eDim0] * abs_Rab(0,i) + a.l[eDim1]*abs_Rab(1,i) + a.l[eDim2]*abs_Rab(2,i);
    rb = b.l[i];
    Rab.get_column(i, Rab_col);
    if (std::fabs(Vector3d::dot(t, Rab_col)) > ra + rb)
    {
      FILE_LOG(LOG_BV) << "OBBs do not intersect" << endl;
      return false;
    }
  }
  
  // test axis L = A0 x B0
  ra = a.l[eDim1] * abs_Rab(Z,X) + a.l[eDim2] * abs_Rab(Y,X);
  rb = b.l[eDim1] * abs_Rab(X,Z) + b.l[eDim2] * abs_Rab(X,Y);
  if (std::fabs(t[Z]*Rab(Y,X) - t[Y]*Rab(Z,X)) > ra + rb)
  {
    FILE_LOG(LOG_BV) << "OBBs do not intersect" << endl;
    return false;
  }

  // test axis L = A0 x B1
  ra = a.l[eDim1] * abs_Rab(Z,Y) + a.l[eDim2] * abs_Rab(Y,Y);
  rb = b.l[eDim0] * abs_Rab(X,Z) + b.l[eDim2] * abs_Rab(X,X);
  if (std::fabs(t[Z]*Rab(Y,Y) - t[Y]*Rab(Z,Y)) > ra + rb)
  {
    FILE_LOG(LOG_BV) << "OBBs do not intersect" << endl;
    return false;
  }

  // test axis L = A0 x B2
  ra = a.l[eDim1] * abs_Rab(Z,Z) + a.l[eDim2] * abs_Rab(Y,Z);
  rb = b.l[eDim0] * abs_Rab(X,Y) + b.l[eDim1] * abs_Rab(X,X);
  if (std::fabs(t[Z]*Rab(Y,Z) - t[Y]*Rab(Z,Z)) > ra + rb)
  {
    FILE_LOG(LOG_BV) << "OBBs do not intersect" << endl;
    return false;
  }

  // test axis L = A1 x B0
  ra = a.l[eDim0] * abs_Rab(Z,X) + a.l[eDim2] * abs_Rab(X,X);
  rb = b.l[eDim1] * abs_Rab(Y,Z) + b.l[eDim2] * abs_Rab(Y,Y);
  if (std::fabs(t[X]*Rab(Z,X) - t[Z]*Rab(X,X)) > ra + rb)
  {
    FILE_LOG(LOG_BV) << "OBBs do not intersect" << endl;
    return false;
  }

  // test axis L = A1 x B1
  ra = a.l[eDim0] * abs_Rab(Z,Y) + a.l[eDim2] * abs_Rab(X,Y);
  rb = b.l[eDim0] * abs_Rab(Y,Z) + b.l[eDim2] * abs_Rab(Y,X);
  if (std::fabs(t[X]*Rab(Z,Y) - t[Z]*Rab(X,Y)) > ra + rb)
  {
    FILE_LOG(LOG_BV) << "OBBs do not intersect" << endl;
    return false;
  }

  // test axis L = A1 x B2
  ra = a.l[eDim0] * abs_Rab(Z,Z) + a.l[eDim2] * abs_Rab(X,Z);
  rb = b.l[eDim0] * abs_Rab(Y,Y) + b.l[eDim1] * abs_Rab(Y,X);
  if (std::fabs(t[X]*Rab(Z,Z) - t[Z]*Rab(X,Z)) > ra + rb)
  {
    FILE_LOG(LOG_BV) << "OBBs do not intersect" << endl;
    return false;
  }

  // test axis L = A2 x B0
  ra = a.l[eDim0] * abs_Rab(Y,X) + a.l[eDim1] * abs_Rab(X,X);
  rb = b.l[eDim1] * abs_Rab(Z,Z) + b.l[eDim2] * abs_Rab(Z,Y);
  if (std::fabs(t[Y]*Rab(X,X) - t[X]*Rab(Y,X)) > ra + rb)
  {
    FILE_LOG(LOG_BV) << "OBBs do not intersect" << endl;
    return false;
  }

  // test axis L = A2 x B1
  ra = a.l[eDim0] * abs_Rab(Y,Y) + a.l[eDim1] * abs_Rab(X,Y);
  rb = b.l[eDim0] * abs_Rab(Z,Z) + b.l[eDim2] * abs_Rab(Z,X);
  if (std::fabs(t[Y]*Rab(X,Y) - t[X]*Rab(Y,Y)) > ra + rb)
  {
    FILE_LOG(LOG_BV) << "OBBs do not intersect" << endl;
    return false;
  }

  // test axis L = A2 x B2
  ra = a.l[eDim0] * abs_Rab(Y,Z) + a.l[eDim1] * abs_Rab(X,Z);
  rb = b.l[eDim0] * abs_Rab(Z,Y) + b.l[eDim1] * abs_Rab(Z,X);
  if (std::fabs(t[Y]*Rab(X,Z) - t[X]*Rab(Y,Z)) > ra + rb)
  {
    FILE_LOG(LOG_BV) << "OBBs do not intersect" << endl;
    return false;
  }

  FILE_LOG(LOG_BV) << "OBBs intersect" << endl;

  // since no separating axis is found, OBBs must be intersecting
  return true;
}

/// Outputs the OBB in VRML format to the given stream
std::ostream& OBB::to_vrml(std::ostream& out, const Pose3d& T) const
{
  const unsigned X = 0, Y = 1, Z = 2;

  // get translation and axis angle for T
  const Origin3d& tx = T.x;
  AAngled rot = T.q;

  // make the OBB orientation matrix right handed
  Matrix3d Rr = R;
  Point3d x = Rr.get_column(X);
  Point3d y = Rr.get_column(Y);
  Vector3d z = Vector3d::cross(x, y);
  Rr.set_column(Z, z);

  // convert the orientation to axis-angle representation
  AAngled aa = Rr;

  // setup the vertices
  list<Point3d> vertices;
  vertices.push_back(Point3d(-1,-1,+1));
  vertices.push_back(Point3d(+1,-1,+1));
  vertices.push_back(Point3d(-1,+1,+1));
  vertices.push_back(Point3d(+1,+1,+1));
  vertices.push_back(Point3d(-1,+1,-1));
  vertices.push_back(Point3d(+1,+1,-1));
  vertices.push_back(Point3d(+1,-1,-1));
  vertices.push_back(Point3d(-1,-1,-1));
  
  // write to the stream
  out << "Transform { " << endl;
  out << "  translation " << tx[X] << " " << tx[Y] << " " << tx[Z] << endl;
  out << "  rotation " << rot.x << " " << rot.y << " " << rot.z << " " << rot.angle << endl;
  out << "  children [ Transform { " << endl;
  out << "    translation " << center[X] << " " << center[Y] << " " << center[Z] << endl;
  out << "    rotation " << aa.x << " " << aa.y << " " << aa.z << " " << aa.angle << endl;
  out << "    children [" << endl;
  out << "      Transform { " << endl;
  out << "        scale " << l[0] << " " << l[1] << " " << l[2] << endl;
  out << "        children [ " << endl;
  out << "          Shape {" << endl;
  out << "            geometry IndexedLineSet {" << endl;
  out << "              coord Coordinate { point [ ";
  BOOST_FOREACH(const Point3d& vertex, vertices)
    out << vertex[X] << " " << vertex[Y] << " " << vertex[Z] << ", ";
  out << "                ] }" << endl;
  out << "              coordIndex [ 0, 1, 3, 2, -1, 1, 6, 5, 3, -1, 0, 2, 4, 7, -1,";
  out << "                2, 3, 5, 4, -1, 0, 7, 6, 1, -1, 4, 5, 6, 7, -1] } }" << endl; 
  out << "           ] }" << endl; // end transform
  double scale = std::max(l[0], std::max(l[1], l[2]));
  if (false && !this->children.empty())
  {
    out << "      Transform { " << endl;
    out << "        scale " << scale << " " << scale << " " << scale << endl;
    out << "        children Text {" << endl;
    out << "        string " << this << endl;
    out << "      } }" << std::endl;
  }
  out << "] } }" << std::endl;
  if (this->children.empty() && this->userdata)
  {
    // cast it as a list of ThickTriangle objects -- NOTE: this is potentially
    // dangerous (if userdata is a different type)
    const list<ThickTriangle>& tt = *static_pointer_cast<list<ThickTriangle> >(this->userdata);
    out << "# underlying triangles" << endl;
    unsigned tri_idx = 0;
    out << "Shape {" << endl;
    out << "  geometry IndexedFaceSet {" << endl;
    out << "  solid FALSE" << endl;
    out << "  coord Coordinate { point [ ";
    BOOST_FOREACH(const ThickTriangle& ttri, tt)
    {
      out << "    " << ttri.tri.a[X] << " " << ttri.tri.a[Y] << " " << ttri.tri.a[Z] << ", ";
      out << "    " << ttri.tri.b[X] << " " << ttri.tri.b[Y] << " " << ttri.tri.b[Z] << ", ";
      out << "    " << ttri.tri.c[X] << " " << ttri.tri.c[Y] << " " << ttri.tri.c[Z] << ", ";
    }
    out << " ] }" << endl;
    out << "      coordIndex [ " << endl;
    for (unsigned i=0; i< tt.size(); i++, tri_idx+= 3)
      out << tri_idx << ", " << (tri_idx+1) << ", " << (tri_idx+2) << ", -1," << endl;
    out << " ] } }" << endl;
  }
  else
  {
// TODO: disabling writing triangles for non-leaf nodes
/*
    std::stack<OBBPtr> s;
    BOOST_FOREACH(OBBPtr obb, this->children)
      s.push(obb);
    while (!s.empty())
    {
      // add children to the stack
      OBBPtr o = s.top();
      s.pop();
      BOOST_FOREACH(OBBPtr obb, o->children)
        s.push(obb);
       
      // look for leaf node
      if (o->children.empty() && o->userdata)
      {
        // cast it as a list of ThickTriangle objects -- NOTE: this is potentially
        // dangerous (if userdata is a different type)
        const list<ThickTriangle>& tt = *static_pointer_cast<list<ThickTriangle> >(o->userdata);
        unsigned tri_idx = 0;
        out << "Shape {" << endl;
        out << "  geometry IndexedFaceSet {" << endl;
        out << "  solid FALSE" << endl;
        out << "  coord Coordinate { point [ ";
        BOOST_FOREACH(const ThickTriangle& ttri, tt)
        {
          out << "    " << ttri.tri.a[X] << " " << ttri.tri.a[Y] << " " << ttri.tri.a[Z] << ", ";
          out << "    " << ttri.tri.b[X] << " " << ttri.tri.b[Y] << " " << ttri.tri.b[Z] << ", ";
          out << "    " << ttri.tri.c[X] << " " << ttri.tri.c[Y] << " " << ttri.tri.c[Z] << ", ";
        }
        out << " ] }" << endl;
        out << "      coordIndex [ " << endl;
        for (unsigned i=0; i< tt.size(); i++, tri_idx+= 3)
          out << tri_idx << ", " << (tri_idx+1) << ", " << (tri_idx+2) << ", -1," << endl;
         out << " ] } }" << endl;
       }
     }
*/
  }
  out << "] }" << endl;

  return out;
}

/// Loads an OBB hierarchy from a XML tree
OBBPtr OBB::load_from_xml(shared_ptr<const XMLTree> root)
{
  // setup some reasonable defaults
  Point3d center = Point3d::zero();
  Vector3d lengths = Vector3d::zero();
  Matrix3d R = Matrix3d::identity();

  // create a new OBB
  OBBPtr obb(new OBB);
  
  // read the center, length, and axes attributes
  const XMLAttrib* cattrib = root->get_attrib("center");
  if (cattrib)
    center = cattrib->get_point_value();

  // read the lengths attribute
  const XMLAttrib* lattrib = root->get_attrib("lengths");
  if (lattrib)
    lattrib->get_vector_value(lengths);

  // read the axes attribute
  const XMLAttrib* aattrib = root->get_attrib("axes");
  if (aattrib)
    aattrib->get_matrix_value(R);

  // set the properties of the OBB
  obb->center = center;
  obb->l = lengths;
  obb->R = R;

  // if there are child OBB nodes, add them
  list<shared_ptr<const XMLTree> > children = root->find_child_nodes("OBB");
  if (!children.empty())
    BOOST_FOREACH(shared_ptr<const XMLTree> child, children)
      obb->children.push_back(load_from_xml(child));
  else
  {
    // read the triangle(s)
    list<shared_ptr<const XMLTree> > tchildren = root->find_child_nodes("Triangle");
    if (!tchildren.empty())
    {
      shared_ptr<list<ThickTriangle> > tt_list(new list<ThickTriangle>());
      BOOST_FOREACH(shared_ptr<const XMLTree> node, tchildren)
      {
        // read the thickness
        double thickness = 0.0;
        const XMLAttrib* tattr = node->get_attrib("thickness");
        if (!tattr)
          cerr << "OBB::load_from_xml() - no thickness specified in Triangle node" << endl;
        else 
          thickness = tattr->get_real_value();

        // construct the triangle
        Point3d va, vb, vc;
        const XMLAttrib* vaattr = node->get_attrib("vertex1");
        const XMLAttrib* vbattr = node->get_attrib("vertex2");
        const XMLAttrib* vcattr = node->get_attrib("vertex3");
        if (!vaattr)
          cerr << "OBB::load_from_xml() - missing vertex in Triangle node" << endl;    
        else
          va = vaattr->get_point_value();
        if (!vbattr)
          cerr << "OBB::load_from_xml() - missing vertex in Triangle node" << endl;    
        else
          vb = vbattr->get_point_value();
        if (!vcattr)
          cerr << "OBB::load_from_xml() - missing vertex in Triangle node" << endl;    
        else
          vc = vcattr->get_point_value();
      
        Triangle tri(va, vb, vb);
        tt_list->push_back(ThickTriangle(tri, thickness));
      }
      obb->userdata = tt_list;
    }
  }

  return obb; 
}

/// Saves a OBB hiearchy to a XMLTree
XMLTreePtr OBB::save_to_xml_tree() const
{
  // create a XML tree
  XMLTreePtr tree(new XMLTree("OBB"));

  // set the center, length, and orientation attributes
  tree->attribs.insert(XMLAttrib("lengths", this->l));
  tree->attribs.insert(XMLAttrib("center", this->center));
  tree->attribs.insert(XMLAttrib("axes", this->R));

  // call method recursively for children
  BOOST_FOREACH(BVPtr child, this->children)
    tree->add_child(dynamic_pointer_cast<OBB>(child)->save_to_xml_tree());

  // if this OBB is a leaf node, cast the userdata
  if (this->children.empty() && this->userdata)
  {
    const list<ThickTriangle>& ttris = *static_pointer_cast<list<ThickTriangle> >(this->userdata);
    BOOST_FOREACH(const ThickTriangle& t, ttris)
    {
      XMLTreePtr tri(new XMLTree("Triangle"));
      // NOTE: we are using arbitrary thickness
      tri->attribs.insert(XMLAttrib("thickness", NEAR_ZERO));
      tri->attribs.insert(XMLAttrib("vertex1", t.tri.a));
      tri->attribs.insert(XMLAttrib("vertex2", t.tri.b));
      tri->attribs.insert(XMLAttrib("vertex3", t.tri.c));
      tree->add_child(tri);
    }
  } 

  return tree;
}

/// Calculates the velocity-expanded OBB for a body
BVPtr OBB::calc_vel_exp_BV(CollisionGeometryPtr g, double dt, const Twistd& v) const
{
  const unsigned X = 0, Y = 1, Z = 2;

  // for this to work, OBB must be defined relative to g 
  assert(center.pose == g.get_pose());

  // get the corresponding body
  RigidBodyPtr b = dynamic_pointer_cast<RigidBody>(g->get_single_body());

  // if the body does not move, just return the OBB
  if (!b->is_enabled())
  {
    FILE_LOG(LOG_BV) << "OBB::calc_vel_exp_OBB() entered" << endl;
    FILE_LOG(LOG_BV) << "  original/expanded bounding box: " << endl << *get_this();
    FILE_LOG(LOG_BV) << "OBB::calc_vel_exp_OBB() exited" << endl;

    return const_pointer_cast<OBB>(get_this());
  }

  // transform the velocity to the global frame
  Twistd v0 = Pose3d::transform(v.pose, GLOBAL, v);

  // get matrix for transforming vectors from b's frame to world frame
  shared_ptr<const Pose3d> wTb = b->get_pose();

  // copy the OBB, expanded by linear velocity
  OBBPtr o(new OBB);
  if (lv.norm() <= NEAR_ZERO/dt) 
    *o = *get_this();
  else
    *o = OBB(*get_this(), lv*dt);

  FILE_LOG(LOG_BV) << "OBB::calc_vel_exp_OBB() entered" << endl;
  FILE_LOG(LOG_BV) << "  original bounding box: " << endl << *get_this();
  FILE_LOG(LOG_BV) << "  linear velocity expanded bounding box: " << endl << *o;

  // if there is no angular velocity, nothing more needs to be done
  double av_norm = av.norm();
  if (av_norm < NEAR_ZERO)
  {
    FILE_LOG(LOG_BV) << " -- angular velocity near zero" << endl;
    FILE_LOG(LOG_BV) << "OBB::calc_vel_exp_OBB() exited" << endl;

    return o;
  }

  // get the position of the center-of-mass of the body
  Point3d com = b->get_position();

  // determine vertices in OBB coordinates
  const unsigned OBB_VERTS = 8;
  Point3d verts[OBB_VERTS];
  verts[0] = Point3d(-o->l[X], -o->l[Y], -o->l[Z]);
  verts[1] = Point3d(-o->l[X], -o->l[Y], o->l[Z]);
  verts[2] = Point3d(-o->l[X], o->l[Y], -o->l[Z]);
  verts[3] = Point3d(-o->l[X], o->l[Y], o->l[Z]);
  verts[4] = Point3d(o->l[X], -o->l[Y], -o->l[Z]);
  verts[5] = Point3d(o->l[X], -o->l[Y], o->l[Z]);
  verts[6] = Point3d(o->l[X], o->l[Y], -o->l[Z]);
  verts[7] = Point3d(o->l[X], o->l[Y], o->l[Z]);

  FILE_LOG(LOG_BV) << "linearly expanded OBB vertices:" << endl;
  if (LOGGING(LOG_BV))
    for (unsigned i=0; i< OBB_VERTS; i++)
      FILE_LOG(LOG_BV) << "  " << i << ": " << verts[i] << endl; 

  // setup transform from OBB orientation to world orientation
  Matrix3d wTo = Matrix3d(wTb->q) * o->R;

  // setup the angular velocity in the OBB frame
  Vector3d w = wTo.transpose_mult(av);

  // normalize the angular velocity vector
  Vector3d wn = Vector3d::normalize(w);

  // setup projection matrix
  Matrix3d P;
  Opsd::outer_prod(wn, -wn, P);
  P += Matrix3d::identity();

  // determine c
  Vector3d c = P * Vector3d(std::fabs(w[X])+(double) 1.0, w[Y], w[Z]);
  double cnorm = c.norm();
  Vector3d chat = Vector3d::zero();
  if (cnorm > NEAR_ZERO) 
    chat = c/cnorm;

  // determine d 
  Vector3d d = P * Vector3d(w[X], std::fabs(w[Y]) + (double) 1.0, w[Z]);
  double dnorm = d.norm();
  Vector3d dhat = Vector3d::zero();
  if (dnorm > NEAR_ZERO) 
    dhat = d/dnorm;

  // determine e
  Vector3d e = P * Vector3d(w[X], w[Y], std::fabs(w[Z]) + (double) 1.0);
  double enorm = e.norm();
  Vector3d ehat = Vector3d::zero();
  if (enorm > NEAR_ZERO)
    ehat = e/enorm;

  // get the center of the OBB (with respect to the OBB frame)
  Point3d center_o = o->R.transpose_mult(o->center); 

  // compute the current minima and maxima along the three OBB axes
  Point3d min_o = center_o - o->l;
  Point3d max_o = center_o + o->l;

  // process all vertices
  for (unsigned i=0; i< OBB_VERTS; i++)
  {
    // get the radial vector
    Vector3d r = center_o + verts[i];

    // calculate the new lengths
    Vector3d lprime;
    const double wnxr = Vector3d::cross(wn, r).norm();
    lprime[X] = (cnorm > NEAR_ZERO) ? (chat*wnxr)[X] : (double) 0.0;
    lprime[Y] = (dnorm > NEAR_ZERO) ? (dhat*wnxr)[Y] : (double) 0.0;
    lprime[Z] = (enorm > NEAR_ZERO) ? (ehat*wnxr)[Z] : (double) 0.0;

    // compute the OBB center
    Point3d center_new = wn * wn.dot(r); 

    // compute new minima and maxima
    Point3d min_i = center_new - lprime;
    Point3d max_i = center_new + lprime;

    FILE_LOG(LOG_BV) << "min_o: " << min_o << "  max_o: " << max_o << endl;
    FILE_LOG(LOG_BV) << "min_i: " << min_i << "  max_i: " << max_i << endl;

    // merge the OBBs
    Point3d minimum, maximum;
    for (unsigned j=X; j<= Z; j++)
    {
      minimum[j] = std::min(min_i[j], min_o[j]);
      maximum[j] = std::max(max_i[j], max_o[j]);
    }

    // compute the new center and lengths
    o->center = (maximum+minimum)*0.5;
    o->l = (maximum-minimum)*0.5;

    // store the new maximum and minimum
    max_o = maximum;
    min_o = minimum;
    
    FILE_LOG(LOG_BV) << "expanding vertex " << verts[i] << endl;
    FILE_LOG(LOG_BV) << "  forming new OBB..." << endl;
    FILE_LOG(LOG_BV) << "    l': " << lprime << endl;
    FILE_LOG(LOG_BV) << "    center: " << center_new << endl;
    FILE_LOG(LOG_BV) << "  ...unioning with running OBB" << endl;
    FILE_LOG(LOG_BV) << "    unioned l: " << o->l << endl;
    FILE_LOG(LOG_BV) << "    unioned center: " << (o->R * o->center) << endl;
  }

  // convert the OBB center to the body frame
  o->center = o->R * o->center;

  FILE_LOG(LOG_BV) << "  angular velocity expanded bounding box: " << endl << *o;
  FILE_LOG(LOG_BV) << "OBB::calc_vel_exp_OBB() exited" << endl;

  // NOTE: the orientation of the bounding box does not change
  return o;
}

/// Gets the lower bounds on the OBB
Point3d OBB::get_lower_bounds(const Pose3d& T)
{
  // get the vertices of the OBB
  const unsigned OBB_VERTS = 8;
  Point3d verts[OBB_VERTS];
  get_vertices(verts);

  // calculate 
  Point3d min = T.transform(verts[0]);
  for (unsigned i=1; i< OBB_VERTS; i++)
  {
    verts[i] = T.transform(verts[i]);
    min[0] = std::min(min[0], verts[i][0]);
    min[1] = std::min(min[1], verts[i][1]);
    min[2] = std::min(min[2], verts[i][2]);
  }

  return min;
}

/// Gets the upper bounds on the OBB
Point3d OBB::get_upper_bounds(const Pose3d& T)
{
  // get the OBB vertices
  const unsigned OBB_VERTS = 8;
  Point3d verts[OBB_VERTS];
  get_vertices(verts);

  // determine the maximum bound
  Point3d max = T.transform(verts[0]); 
  for (unsigned i=1; i< OBB_VERTS; i++)
  {
    verts[i] = T.transform(verts[i]);
    max[0] = std::max(max[0], verts[i][0]);
    max[1] = std::max(max[1], verts[i][1]);
    max[2] = std::max(max[2], verts[i][2]);
  }

  return max;
}


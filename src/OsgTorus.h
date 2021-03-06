 // Drawn from Torus.h by John K. Grant
 
 //
 //  Class to make a torus.
 //
 
#ifndef _OSG_TORUS_H_
#define _OSG_TORUS_H_
 
#include <osg/Vec4>
 
namespace osg {
  class Geode;
    
class OsgTorus
{
  public:
    OsgTorus():
      _major_radius(1.0f), _minor_radius(2.0f),
      _start_sweep(osg::inDegrees(0.0f)), _end_sweep(osg::inDegrees(360.0f)),
      _circle_cuts(50), _sweep_cuts(50) {}

    OsgTorus(float inner,float outer,
          float start_angle=0.0f,float end_angle=360.0f,
          unsigned int cross=50,unsigned int sweep=50):
      _major_radius(inner), _minor_radius(outer),
      _start_sweep(start_angle), _end_sweep(end_angle),
      _circle_cuts(cross), _sweep_cuts(sweep) {}
 
    OsgTorus(const OsgTorus& torus) :
      _major_radius(torus._major_radius), _minor_radius(torus._minor_radius),
      _start_sweep(torus._start_sweep), _end_sweep(torus._end_sweep),
      _circle_cuts(torus._circle_cuts), _sweep_cuts(torus._sweep_cuts) {}
 
    inline bool valid() const
    {
      bool validity(false);
      if( (_minor_radius>_major_radius) && (_minor_radius>0.0f) )
        validity = true;
      return( validity );
    }

  inline void set(float i,float o) {_major_radius=i; _minor_radius=o;}

  osg::Geode* operator() () const;

  inline void setColor(const osg::Vec4& c) { _color = c; }
  inline const osg::Vec4& getColor() const { return _color; }

  inline void setMajorRadius(float inner) { _major_radius = inner; }
  inline float getMajorRadius() const { return _major_radius; }

  inline void setMinorRadius(float outer) { _minor_radius = outer; }
  inline float getMinorRadius() const { return _minor_radius; }

  inline void setStartSweep(float angle) { _start_sweep = angle; }
  inline float getStartSweep() const { return _start_sweep; }

  inline void setEndSweep(float angle) { _end_sweep = angle; }
  inline float getEndSweep() const { return _end_sweep; }

  inline void setCircleCuts(int cuts) { _circle_cuts = cuts; }
  inline int getCircleCuts() const { return _circle_cuts; }

  inline void setSweepCuts(int cuts) { _sweep_cuts = cuts; }
  inline int getSweepCuts() const { return _sweep_cuts; }

  virtual ~OsgTorus() {}
 
  private:
    float _major_radius, _minor_radius;
    float _start_sweep, _end_sweep;
    unsigned int _circle_cuts, _sweep_cuts;
    osg::Vec4 _color;
}; // end class
 
}; // namespace osg 
 
 
#endif // _OSG_TORUS_H_


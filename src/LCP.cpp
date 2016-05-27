/****************************************************************************
 * Copyright 2013 Evan Drumwright
 * This library is distributed under the terms of the Apache V2.0 
 * License (obtainable from http://www.apache.org/licenses/LICENSE-2.0).
 ****************************************************************************/

#include <numeric>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <sstream>
#include <fstream>
#include <boost/algorithm/minmax.hpp>
#include <boost/algorithm/minmax_element.hpp>
#include <boost/foreach.hpp>
#include <Ravelin/select>
#include <Ravelin/LinAlgd.h>
#include <Ravelin/SingularException.h>
#include <Ravelin/NonsquareMatrixException.h>
#include <Ravelin/NumericalException.h>
#include <Moby/Log.h>
#include <Moby/Constants.h>
#include <Moby/insertion_sort>
#include <Moby/LCP.h>

using namespace Ravelin;
using namespace Moby;
using std::endl;
using std::vector;
using std::pair;
using std::map;
using std::make_pair;
using boost::shared_ptr;

// Sole constructor
LCP::LCP()
{
}

/// Sign function for Keller's algorithm
double LCP::sgn(double x)
{
  if (x > NEAR_ZERO)
    return 1.0;
  else if (x < -NEAR_ZERO)
    return -1.0;
  else
    return 0.0;
}

// compute z and w
void LCP::compute_z_w(const MatrixNd& M, const VectorNd& q, const VectorNd& zalpha, const VectorNd& l, const VectorNd& u, const std::vector<unsigned>& alpha, const std::vector<unsigned>& betal, const std::vector<unsigned>& betau, VectorNd& z, VectorNd& w)
{ 
  // resize z as necessary
  const unsigned N = q.size();
  z.resize(N);

  // set nonbasic components of z
  z.set(alpha.begin(), alpha.end(), zalpha);

  // set basic components of z 
  for (unsigned i=0; i< N; i++)
  {
    // if the index is in the nonbasic set, skip it
    if (std::binary_search(alpha.begin(), alpha.end(), i))
      continue;

    // the index is in the basic set, find whether it is in betal 
    if (std::binary_search(betal.begin(), betal.end(), i))
      z[i] = l[i];
    else 
    {
      assert(std::binary_search(betau.begin(), betau.end(), i));
      z[i] = u[i];
    }
  }

  // finally, compute w
  M.mult(z, w) += q;
}

static std::string to_string(const vector<unsigned>& indices)
{
  std::ostringstream str;
  for (unsigned i=0; i< indices.size(); i++)
    str << " " << indices[i];
  return str.str();
} 

static void verify(const VectorNd& l, const VectorNd& u, const VectorNd& z)
{
return;
  for (unsigned i=0; i< z.size(); i++)
  {
    assert (z[i] + NEAR_ZERO >= l[i]);
    assert (z[i] - NEAR_ZERO <= u[i]);
  } 
} 

/// Keller's fast pivoting algorithm for LCPs with positive semi-definite matrices and lower and upper bounds on variables 
bool LCP::mlcp_keller(const MatrixNd& M, const VectorNd& q, const VectorNd& l, const VectorNd& u, VectorNd& z, double zero_tol)
{
  const unsigned N = q.rows();
  const unsigned UINF = std::numeric_limits<unsigned>::max();
  const double INF = std::numeric_limits<double>::max();
  double theta, theta0, theta1, theta2, theta3;
  std::vector<int> pivwork;

  FILE_LOG(LOG_OPT) << "LCP::mlcp_keller() entered" << std::endl;

  // look for trivial solution
  if (N == 0)
  {
    FILE_LOG(LOG_OPT) << "LCP::mlcp_keller() - empty problem" << std::endl;
    z.resize(0);
    return true;
  }

  // set zero tolerance if necessary
  if (zero_tol < 0.0)
    zero_tol = M.rows() * M.norm_inf() * std::numeric_limits<double>::epsilon();

  // prepare to setup basic and nonbasic variable indices for z
  _alpha.clear();
  _beta.clear();
  _betal.clear();
  _betau.clear();

  // setup alpha, beta
  for (unsigned i=0; i< q.size(); i++)
  {
    if (l[i] == -INF && u[i] == INF)
      _alpha.push_back(i);
    else
    {
      if (l[i] == -INF)
        _betau.push_back(i);
      else
        _betal.push_back(i);
      _beta.push_back(i);
    }
  }

  // solve for zalpha 
  M.select_square(_alpha.begin(), _alpha.end(), _Msub);
  if (!_LA.factor_LU(_Msub, pivwork))
  {
    FILE_LOG(LOG_OPT) << "LCP::mlcp_keller() - linear system solve failed" << std::endl;
    return false;
  }
  q.select(_alpha.begin(), _alpha.end(), _zalpha);
  _zalpha.negate();
  _LA.solve_LU_fast(_Msub, false, pivwork, _zalpha);

  // compute z and w
  compute_z_w(M, q, _zalpha, l, u, _alpha, _betal, _betau, z, _w); 
  #ifndef NDEBUG
  verify(l, u, z);
  #endif

  // pivot up to the prescribed number of times
  const unsigned MAX_PIV = 2*N;
  for (pivots=0; pivots < MAX_PIV; pivots++)
  {
    // determine sl 
    unsigned sl = UINF;
    double wplus_min = INF;
    for (unsigned i=0; i< _betal.size(); i++)
      if (_w[_betal[i]] < wplus_min)
      {
        sl = _betal[i];
        wplus_min = _w[sl];
      }

    // determine su
    unsigned su = UINF;
    double wminus_max = -INF;
    for (unsigned i=0; i< _betau.size(); i++)
      if (_w[_betau[i]] > wminus_max)
      {
        su = _betau[i];
        wminus_max = _w[su];
      }

    // determine s 
    unsigned s;
    if (sl == UINF)
      s = su;
    else if (su == UINF || _w[sl] < -_w[su])
      s = sl;
    else
      s = su; 

    // if there is no violation of w, algorithm is successful (feasibility of
    // z is always maintained)
    if (wminus_max < zero_tol && wplus_min > -zero_tol)
    {
/*
      // solve for zalpha 
      M.select_square(_alpha.begin(), _alpha.end(), _Msub);
      q.select(_alpha.begin(), _alpha.end(), _zalpha);
      _zalpha.negate();
      try
      {
        _LA.solve_LU_fast(_Msub, false, pivwork, _zalpha);
      }
      catch (SingularException e)
      {
        FILE_LOG(LOG_OPT) << "LCP::mlcp_keller() - linear system solve failed" << std::endl;
        return false;
      }
*/
      // compute z and w
      compute_z_w(M, q, _zalpha, l, u, _alpha, _betal, _betau, z, _w); 
      #ifndef NDEBUG
      verify(l, u, z);
      #endif

      if (LOGGING(LOG_OPT))
      {
        FILE_LOG(LOG_OPT) << "z: " << z << std::endl;
        FILE_LOG(LOG_OPT) << "predicted w: " << _w << std::endl;
        M.mult(z, _workv) += q;
        FILE_LOG(LOG_OPT) << "true w: " << _workv << std::endl;
      }
      return true;
    }
    else
      FILE_LOG(LOG_OPT) << "w violation: " << std::fabs(_w[s]) << std::endl;

    // compute p
    double p = -sgn(_w[s]);

    // verify p
    assert(p != 0.0);

    do
    {
      FILE_LOG(LOG_OPT) << "non-basic (alpha) indices: " << to_string(_alpha) << std::endl;
      FILE_LOG(LOG_OPT) << "basic (beta) indices: " << to_string(_beta) << std::endl;
      FILE_LOG(LOG_OPT) << "basic lower (beta) indices: " << to_string(_betal) << std::endl;
      FILE_LOG(LOG_OPT) << "basic upper (beta) indices: " << to_string(_betau) << std::endl;
      FILE_LOG(LOG_OPT) << "s: " << s << " sl: " << sl << " su: " << su << std::endl;

      // compute valpha
      M.get_column(s, _workv);
      _workv.select(_alpha.begin(), _alpha.end(), _valpha);
      _valpha.negate();
      _LA.solve_LU_fast(_Msub, false, pivwork, _valpha);

      // get row of M
      M.get_row(s, _workv);
      _workv.select(_alpha.begin(), _alpha.end(), _workv2);

      // compute rho s
      double rho_s = M(s,s) + _workv2.dot(_valpha);
      
      // reset thetas
      if (p < 0)
        theta0 = theta1 = theta2 = theta3 = -INF;
      else
        theta0 = theta1 = theta2 = theta3 = INF;

      // potentially update theta0
      FILE_LOG(LOG_OPT) << "rho_s: " << rho_s << "  w[s]: " << _w[s] << std::endl;
      if (rho_s > NEAR_ZERO)
        theta0 = -_w[s]/rho_s;

      // potentially update theta1
      if (p > 0)
        theta1 = u[s] - z[s];
      else
        theta1 = l[s] - z[s];

      // compute theta2 and theta3 (and r2 and r3)
      l.select(_alpha.begin(), _alpha.end(), _lalpha);
      u.select(_alpha.begin(), _alpha.end(), _ualpha);
      unsigned r2 = UINF, r3 = UINF;
      for (unsigned i=0; i< _valpha.size(); i++)
      {
        // first, attempt to update r2 / theta2
        if (p * _valpha[i] < -zero_tol)
        {
          double cand = p * (_lalpha[i] - _zalpha[i])/_valpha[i];
          if (r2 == UINF || cand < theta2)
          {
            r2 = _alpha[i];
            theta2 = cand;
            assert(theta2 > -NEAR_ZERO);
          }
        }
        else if (p * _valpha[i] > zero_tol)
        {
          double cand = p * (_ualpha[i] - _zalpha[i])/_valpha[i];
          if (r3 == UINF || cand < theta3)
          {
            r3 = _alpha[i];
            theta3 = cand;
            assert(theta3 > -NEAR_ZERO);
          }
        }
      }

      // compute theta
      FILE_LOG(LOG_OPT) << "theta0: " << theta0 << " theta1: " << theta1 << " theta2: " << theta2 << " theta3: " << theta3 << std::endl;
      double thetas[4] = { p*theta0, p*theta1, p*theta2, p*theta3 };
      theta = p * (*std::min_element(thetas, thetas+4));

      // look for ray termination
      if (theta == INF || theta == -INF)
      {
        FILE_LOG(LOG_OPT) << "LCP::mlcp_keller() - terminated on ray" << std::endl;
        return false;
      }

      // update z and w
      _valpha *= theta;
      _zalpha += _valpha;
      _w[s] += theta * rho_s;
      M.select(_beta.begin(), _beta.end(), _alpha.begin(), _alpha.end(), _Mba);
      M.get_column(s, _workv);
      _workv.select(_beta.begin(), _beta.end(), _workv2);
      _workv2 *= theta;
      _w.select(_beta.begin(), _beta.end(), _workv);
      _workv += _workv2;    // _wbeta += theta*Mbs
      _Mba.mult(_valpha, _workv2);
      _workv += _workv2;    // _wbeta += Mba*valpha
      _w.set(_beta.begin(), _beta.end(), _workv);

      // update z
      FILE_LOG(LOG_OPT) << "z: " << z << std::endl;
      z.set(_alpha.begin(), _alpha.end(), _zalpha);
      #ifndef NDEBUG
      verify(l, u, z);
      #endif

      // update w further if theta = theta0 (also update indices) ...
      if (theta == theta0)
      {
        _w[s] = 0.0;
        _alpha.push_back(s);
        insertion_sort(_alpha.begin(), _alpha.end());
        assert(std::lower_bound(_beta.begin(), _beta.end(), s) != _beta.end());

        // erase from beta
        _beta.erase(std::lower_bound(_beta.begin(), _beta.end(), s));

        // erase from betal
        vector<unsigned>::iterator siter;
        siter = std::lower_bound(_betal.begin(), _betal.end(), s);
        if (siter != _betal.end())
          _betal.erase(siter);

        // erase from betau
        siter = std::lower_bound(_betau.begin(), _betau.end(), s);
        if (siter != _betau.end())
          _betau.erase(siter);
      }
      // ...or if theta = theta1 (also update indices) ...
      else if (theta == theta1)
      {
        // remove s from alpha
        assert(std::lower_bound(_alpha.begin(), _alpha.end(), s) != _alpha.end());
        _alpha.erase(std::lower_bound(_alpha.begin(), _alpha.end(), s));

        // add s to beta
        _beta.push_back(s);
        insertion_sort(_beta.begin(), _beta.end());

        if (p > 0.0)
        {
          z[s] = u[s];

          // add s to beta u
          _betau.push_back(s);
          insertion_sort(_betau.begin(), _betau.end());
        }
        else
        {
          z[s] = l[s];

          // add s to beta l 
          _betal.push_back(s);
          insertion_sort(_betal.begin(), _betal.end());
        }
      }
      // ...or if theta = theta2 (also update indices) ...
      else if (theta == theta2)
      {
        // update z
        z[r2] = l[r2];

        // remove r2 from alpha
        assert(std::lower_bound(_alpha.begin(), _alpha.end(), r2) != _alpha.end());
        _alpha.erase(std::lower_bound(_alpha.begin(), _alpha.end(), r2));

        // add r to beta l 
        _betal.push_back(r2);
        insertion_sort(_betal.begin(), _betal.end());

        // add r to beta
        _beta.push_back(r2);
        insertion_sort(_beta.begin(), _beta.end());
      }
      // ...or if theta = theta3 (also update indices) ...
      else if (theta == theta3)
      {
        // update z
        z[r3] = u[r3];

        // remove r3 from alpha
        assert(std::lower_bound(_alpha.begin(), _alpha.end(), r3) != _alpha.end());
        _alpha.erase(std::lower_bound(_alpha.begin(), _alpha.end(), r3));

        // add r to beta u 
        _betau.push_back(r3);
        insertion_sort(_betau.begin(), _betau.end());

        // add r to beta
        _beta.push_back(r3);
        insertion_sort(_beta.begin(), _beta.end());
      }
      else
        assert(false);

      FILE_LOG(LOG_OPT) << "predicted w: " << _w << std::endl;
  
      // redetermine z -- first solve for zalpha
      M.select_square(_alpha.begin(), _alpha.end(), _Msub);
      if (!_LA.factor_LU(_Msub, pivwork))
      {
        FILE_LOG(LOG_OPT) << "LCP::mlcp_keller() - linear system solve failed" << std::endl;
        return false;
      }
      q.select(_alpha.begin(), _alpha.end(), _zalpha);
      _zalpha.negate();
      _LA.solve_LU_fast(_Msub, false, pivwork, _zalpha);

      // compute z and w
      compute_z_w(M, q, _zalpha, l, u, _alpha, _betal, _betau, z, _w); 
    }
    while (theta != theta0);    
  }

  // if we're here, maximum pivots exceeded
  FILE_LOG(LOG_OPT) << "LCP::lcp_fast() - maximum allowable pivots exceeded" << std::endl;

  // if we're here, then the maximum number of pivots has been exceeded
  return false;
}

/// Fast pivoting algorithm for LCPs with positive semi-definite matrices and lower and upper bounds on variables 
bool LCP::mlcp_fast(const MatrixNd& M, const VectorNd& q, const VectorNd& l, const VectorNd& u, VectorNd& z, double zero_tol)
{
  const unsigned N = q.rows();
  const unsigned UINF = std::numeric_limits<unsigned>::max();
  const double INF = std::numeric_limits<double>::max();

  FILE_LOG(LOG_OPT) << "LCP::mlcp_fast() entered" << std::endl;

  // look for trivial solution
  if (N == 0)
  {
    FILE_LOG(LOG_OPT) << "LCP::mlcp_fast() - empty problem" << std::endl;
    z.set_zero(0);
    return true;
  }

  // set zero tolerance if necessary
  if (zero_tol < 0.0)
    zero_tol = M.rows() * M.norm_inf() * std::numeric_limits<double>::epsilon();

  // prepare to setup basic and nonbasic variable indices for z
  _nonbas.clear();
  _bas.clear();
  _basl.clear();
  _basu.clear();

  // determine whether any lower values are at infinity
  bool nolinf = true;
  for (unsigned i=0; i< N; i++)
    if (l[i] == -INF)
    {
      nolinf = false;
      break;
    }

  // see whether to warm-start
  if (z.size() == q.size())
  {
    FILE_LOG(LOG_OPT) << "LCP::mlcp_fast() - warm starting activated" << std::endl;

    for (unsigned i=0; i< z.size(); i++)
    {
      if (std::fabs(z[i]-l[i]) < zero_tol)
      {
        _basl.push_back(i);
        _bas.push_back(i);
      }
      else if (std::fabs(u[i]-z[i]) < zero_tol)
      {
        _basu.push_back(i);
        _bas.push_back(i);
      }
      else
        _nonbas.push_back(i);
    }

    if (LOGGING(LOG_OPT))
    {
      std::ostringstream str;
      str << " -- non-basic indices:";
      for (unsigned i=0; i< _nonbas.size(); i++)
        str << " " << _nonbas[i];
      FILE_LOG(LOG_OPT) << str.str() << std::endl;
    }
  }
  else if (nolinf)
  {
    // compute M*l + q and compute the minimum element
    M.mult(l, _w) += q; 
    unsigned minw = std::min_element(_w.begin(), _w.end()) - _w.begin();
    if (_w[minw] > -zero_tol)
    {
      FILE_LOG(LOG_OPT) << "LCP::lcp_fast() - trivial solution found" << std::endl;
      z = l;
      return true;
    }

    // setup basic and nonbasic variable indices
    _nonbas.push_back(minw); 
    _basl.resize(N-1);
    for (unsigned i=0, j=0; i< N; i++)
      if (i != minw)
        _basl[j++] = i;
    _basu.resize(0);
    _bas = _basl;
  }
  else
  {
    // set as few non-basic indices as possible
    for (unsigned i=0; i< N; i++)
    {
      if (l[i] == -INF && u[i] == INF)
        _nonbas.push_back(i);
      else if (l[i] == -INF)
      {
        _basu.push_back(i);
        _bas.push_back(i);
      }
      else
      { 
        _basl.push_back(i);
        _bas.push_back(i);
      }
    }
  }
 
  // loop for maximum number of pivots
  const unsigned MAX_PIV = std::max(N*N, (unsigned) 1000);
//  const unsigned MAX_PIV = 2*N;
  for (pivots=0; pivots < MAX_PIV; pivots++)
  {
    if (LOGGING(LOG_OPT))
    {
      std::ostringstream str;
      str << "non-basic indices:";
      for (unsigned i=0; i< _nonbas.size(); i++)
        str << " " << _nonbas[i];
      str << std::endl;
      str << "lower basic indices:";
      for (unsigned i=0; i< _basl.size(); i++)
        str << " " << _basl[i];
      str << std::endl;
      str << "upper basic indices:";
      for (unsigned i=0; i< _basu.size(); i++)
        str << " " << _basu[i];
      str << std::endl;    
      FILE_LOG(LOG_OPT) << "indices before beginning pivot" << std::endl << str.str();
    }

    // setup basic elements of z
    VectorNd zbas, tmpv;
    zbas.resize(_bas.size());
    for (unsigned i=0; i< _bas.size(); i++)
    {
      if (std::binary_search(_basl.begin(), _basl.end(), _bas[i]))
        zbas[i] = l[_bas[i]];
      else
        zbas[i] = u[_bas[i]];
    } 

    // select nonbasic indices
    MatrixNd Mbas;
    M.select_square(_nonbas.begin(), _nonbas.end(), _Msub);
    M.select(_bas.begin(), _bas.end(), _nonbas.begin(), _nonbas.end(), _Mmix);
    M.select_square(_bas.begin(), _bas.end(), Mbas);
    q.select(_nonbas.begin(), _nonbas.end(), _z);
    q.select(_bas.begin(), _bas.end(), _qbas);
    _z.negate();
    FILE_LOG(LOG_OPT) << "M sub: " << std::endl << _Msub;
    FILE_LOG(LOG_OPT) << "rhs: " << _z << std::endl;

    // solve for nonbasic z
    try
    {
      _LA.solve_fast(_Msub, _z);
    }
    catch (SingularException e)
    {
      FILE_LOG(LOG_OPT) << "LCP::mlcp_fast() - linear system solve failed" << std::endl;
      return false;
    }
    FILE_LOG(LOG_OPT) << "sub z: " << _z << std::endl;

    // compute w, which will be the size of the basic indices 
    _Mmix.mult(_z, _w) += _qbas;
    Mbas.mult(zbas, tmpv);
    _w += tmpv;
    FILE_LOG(LOG_OPT) << "sub w: " << _w << std::endl;

    // move _w back to a full vector
    tmpv.set_zero(N);
    tmpv.set(_bas.begin(), _bas.end(), _w);
    _w = tmpv;
    FILE_LOG(LOG_OPT) << "full w: " << _w << std::endl;

    // elements of w corresponding to z at lower bounds (lower basic indices) 
    // must be non-negative; elements of w corresponding to z at upper bounds
    // (upper basic indices) must be non-positive

    // get all elements of w (w+) that correspond to lower basic indices
    _w.select(_basl.begin(), _basl.end(), _wplus);
    FILE_LOG(LOG_OPT) << "w+: " << _wplus << std::endl;

    // get all elements of w (w-) that correspond to upper basic indices
    _w.select(_basu.begin(), _basu.end(), _wminus);
    FILE_LOG(LOG_OPT) << "w-: " << _wminus << std::endl;

    // get the most positive element of w-
    unsigned wminus_max_idx = (_wminus.size() == 0) ? UINF : std::max_element(_wminus.begin(), _wminus.end()) - _wminus.begin();
    double wminus_max = (wminus_max_idx < UINF) ? _wminus[wminus_max_idx] : INF;

    // get the most negative element of w+
    unsigned wplus_min_idx = (_wplus.size() == 0) ? UINF : std::min_element(_wplus.begin(), _wplus.end()) - _wplus.begin();
    double wplus_min = (wplus_min_idx < UINF) ? _wplus[wplus_min_idx] : -INF;
    FILE_LOG(LOG_OPT) << "element of w- closest to infinity: " << wminus_max << std::endl;
    FILE_LOG(LOG_OPT) << "element of w+ closest to -infinity: " << wplus_min << std::endl;

    // get the biggest wviolation and setup wvio
    std::vector<unsigned> wvio;
    double max_wvio = -1.0;
    if (-wminus_max > wplus_min)
    {
      if (wminus_max_idx < UINF)
      {
        max_wvio = -wminus_max;
        wvio.push_back(_basu[wminus_max_idx]);
      }
    }
    else
    {
      if (wplus_min_idx < UINF)
      {
        max_wvio = -wplus_min;
        wvio.push_back(_basl[wplus_min_idx]);
      }
    }  

    if (LOGGING(LOG_OPT))
    {
      std::ostringstream str;
      str << "biggest violating elements of w:";
      for (unsigned i=0; i< wvio.size(); i++)
        str << " " << wvio[i];
      FILE_LOG(LOG_OPT) << str.str() << std::endl;
    }

    // if w is valid
    if (max_wvio < zero_tol)
    {
      // potential solution found; check whether z is valid by checking 
      // whether each non-basic z variable is between l and z

      // set z from basic indices and non-basic variables
      z.resize(N);
      l.select(_basl.begin(), _basl.end(), _workv);
      z.set(_basl.begin(), _basl.end(), _workv);
      u.select(_basu.begin(), _basu.end(), _workv);
      z.set(_basu.begin(), _basu.end(), _workv);
      z.set(_nonbas.begin(), _nonbas.end(), _z);
      FILE_LOG(LOG_OPT) << "full z: " << z << std::endl;

      // get a violated index of z s.t. z < l or z > u, where the index selected
      // exhibits the biggest constraint violation (if multiple indices
      // correspond to biggest constraint violation, one is chosen randomly)
      double max_zvio = -1.0;
      unsigned max_zvio_idx = UINF;
      for (unsigned i=0; i< N; i++)
      {
        // make sure that the index is non-basic
        if (!std::binary_search(_nonbas.begin(), _nonbas.end(), i))
          continue;

        if (z[i] < l[i])
        {
          if (l[i] - z[i] > max_zvio)
          {
            max_zvio = l[i] - z[i];
            max_zvio_idx = i;
          }
        }
        else if (z[i] > u[i])
        {
          if (z[i] - u[i] > max_zvio)
          {
            max_zvio = z[i] - u[i];
            max_zvio_idx = i;
          }
        } 
      }

      // get *all* indices of z that have been violated that much
      if (max_zvio > 0.0)
      {
        std::vector<unsigned> zvio;
        for (unsigned i=0; i< N; i++)
        {
          // make sure that the index is non-basic
          if (!std::binary_search(_nonbas.begin(), _nonbas.end(), i))
            continue;

          if (i == max_zvio_idx)
            zvio.push_back(i);
          else if (l[i] - z[i] + NEAR_ZERO > max_zvio)
            zvio.push_back(i);
          else if (z[i] - u[i] + NEAR_ZERO > max_zvio)
            zvio.push_back(i);
        }
    
        if (LOGGING(LOG_OPT))
        {
          std::ostringstream str;
          str << "biggest violating elements of z:";
          for (unsigned i=0; i< zvio.size(); i++)
            str << " " << zvio[i];
          FILE_LOG(LOG_OPT) << str.str() << std::endl;
        }

        // randomly select an index of z to move from the non-basic to the
        // appropriate basic set
        assert(!zvio.empty());
        unsigned to_bas = zvio[rand() % zvio.size()];

        // move the index from the non-basic to the appropriate basic set (z < l
        // will move to basl, z > u will move to basu)
        if (l[to_bas] - z[to_bas] > z[to_bas] - u[to_bas])
        {
          _basl.push_back(to_bas);
          insertion_sort(_basl.begin(), _basl.end());
        }
        else 
        {
          _basu.push_back(to_bas);
          insertion_sort(_basu.begin(), _basu.end());
        }
        assert(std::binary_search(_nonbas.begin(), _nonbas.end(), to_bas));
        _bas.push_back(to_bas);
        insertion_sort(_bas.begin(), _bas.end());
        _nonbas.erase(std::lower_bound(_nonbas.begin(), _nonbas.end(), to_bas));

        // continue looping
        continue;
      }
      else
      {
        FILE_LOG(LOG_OPT) << "LCP::mlcp_fast() - reporting solution found" << std::endl;
        return true;  // solution found
      }
    }

    // get all of the violated w indices
    for (unsigned i=0; i< _wminus.size(); i++) 
      if (_wminus[i] - zero_tol < -max_wvio)
        wvio.push_back(_basu[i]);
    for (unsigned i=0; i< _wplus.size(); i++)
      if (_wplus[i] + zero_tol > max_wvio)
        wvio.push_back(_basl[i]);

    // pick one of the most violated w indices randomly
    assert(!wvio.empty());
    unsigned to_nbasic = wvio[rand() % wvio.size()];

    // set z from basic indices and non-basic variables
    z.resize(N);
    l.select(_basl.begin(), _basl.end(), _workv);
    z.set(_basl.begin(), _basl.end(), _workv);
    u.select(_basu.begin(), _basu.end(), _workv);
    z.set(_basu.begin(), _basu.end(), _workv);
    z.set(_nonbas.begin(), _nonbas.end(), _z);
    FILE_LOG(LOG_OPT) << "full z: " << z << std::endl;

    // move the violated w index into the basic set, depending on how that
    // index is violated
    _nonbas.push_back(to_nbasic);
    insertion_sort(_nonbas.begin(), _nonbas.end());
    assert(std::binary_search(_bas.begin(), _bas.end(), to_nbasic));
    _bas.erase(std::lower_bound(_bas.begin(), _bas.end(), to_nbasic));
    FILE_LOG(LOG_OPT) << "moving index " << to_nbasic << " to nonbasic set" << std::endl;

    // erase index from basl or basu
    if (_basl.empty())
    {
      FILE_LOG(LOG_OPT) << "-- removing from upper basic set" << std::endl;
      _basu.erase(std::lower_bound(_basu.begin(), _basu.end(), to_nbasic));
    }
    else if (_basu.empty())
    {
      FILE_LOG(LOG_OPT) << "-- removing from lower basic set" << std::endl;
      _basl.erase(std::lower_bound(_basl.begin(), _basl.end(), to_nbasic));
    }
    else
    {
      std::vector<unsigned>::iterator uiter = std::lower_bound(_basl.begin(), _basl.end(), to_nbasic);
      if (*uiter == to_nbasic)
      {
        FILE_LOG(LOG_OPT) << "-- removing from lower basic set" << std::endl;
        _basl.erase(uiter);
      }
      else
      {
        FILE_LOG(LOG_OPT) << "-- removing from upper basic set" << std::endl;
        uiter = std::lower_bound(_basu.begin(), _basu.end(), to_nbasic);
        assert(*uiter == to_nbasic);
        _basu.erase(uiter);
      }
    }

    // get a violated index of z s.t. z < l or z > u, where the index selected
    // exhibits the biggest constraint violation (if multiple indices
    // correspond to biggest constraint violation, one will be chosen randomly)
    double max_zvio = -1.0;
    unsigned max_zvio_idx = UINF;
    for (unsigned i=0; i< N; i++)
    {
      if (z[i] < l[i] - zero_tol)
      {
        if (l[i] - z[i] > max_zvio)
        {
          max_zvio = l[i] - z[i];
          max_zvio_idx = i;
        }
      }
      else if (z[i] > u[i] + zero_tol)
      {
        if (z[i] - u[i] > max_zvio)
        {
          max_zvio = z[i] - u[i];
          max_zvio_idx = i;
        }
      } 
    }

    // get *all* indices of z that have been violated that much
    if (max_zvio_idx < UINF)
    {
      std::vector<unsigned> zvio;
      for (unsigned i=0; i< N; i++)
      {
        if (i == max_zvio_idx)
          zvio.push_back(i);
        else if (l[i] - z[i] + zero_tol > max_zvio)
          zvio.push_back(i);
        else if (z[i] - u[i] + zero_tol > max_zvio)
          zvio.push_back(i);
      }
    
      // randomly select an index of z to move from the non-basic to the
      // appropriate basic set
      assert(!zvio.empty());
      unsigned to_bas = zvio[rand() % zvio.size()];

      // move the index from the non-basic to the appropriate basic set (z < l
      // will move to basl, z > u will move to basu)
      if (l[to_bas] - z[to_bas] > 0.0)
      {
        FILE_LOG(LOG_OPT) << "adding index " << to_bas << " to lower basic set" << std::endl;
        _basl.push_back(to_bas);
        insertion_sort(_basl.begin(), _basl.end());
      }
      else
      {
        FILE_LOG(LOG_OPT) << "adding index " << to_bas << " to upper basic set" << std::endl;
        assert(z[to_bas] - u[to_bas] > 0.0);
        _basu.push_back(to_bas);
        insertion_sort(_basu.begin(), _basu.end());
      }
      FILE_LOG(LOG_OPT) << "removing index " << to_bas << " from non-basic set" << std::endl;
      _bas.push_back(to_bas);
      insertion_sort(_bas.begin(), _bas.end());
      assert(std::binary_search(_nonbas.begin(), _nonbas.end(), to_bas));
      _nonbas.erase(std::lower_bound(_nonbas.begin(), _nonbas.end(), to_bas));
    }
  }

  FILE_LOG(LOG_OPT) << "LCP::mlcp_fast() - maximum allowable pivots exceeded" << std::endl;

  // if we're here, then the maximum number of pivots has been exceeded
  return false;
}

/// Fast pivoting algorithm for LCPs with positive semi-definite matrices few nonzero, nonbasic variables 
bool LCP::lcp_fast(const MatrixNd& M, const VectorNd& q, VectorNd& z, double zero_tol)
{
  const unsigned N = q.rows();
  const unsigned UINF = std::numeric_limits<unsigned>::max();

  FILE_LOG(LOG_OPT) << "LCP::lcp_fast() entered" << std::endl;
  FILE_LOG(LOG_OPT) << "M: " << std::endl << M;
  FILE_LOG(LOG_OPT) << "q: " << q << std::endl;

  // look for trivial solution
  if (N == 0)
  {
    FILE_LOG(LOG_OPT) << "LCP::lcp_fast() - empty problem" << std::endl;
    z.set_zero(0);
    return true;
  }

  // set zero tolerance if necessary
  if (zero_tol < 0.0)
    zero_tol = M.rows() * M.norm_inf() * std::numeric_limits<double>::epsilon();

  // prepare to setup basic and nonbasic variable indices for z
  _nonbas.clear();
  _bas.clear();

  // see whether to warm-start
  if (z.size() == q.size())
  {
    FILE_LOG(LOG_OPT) << "LCP::lcp_fast() - warm starting activated" << std::endl;

    for (unsigned i=0; i< z.size(); i++)
    {
      if (std::fabs(z[i]) < zero_tol)
        _bas.push_back(i);
      else
        _nonbas.push_back(i);
    }

    if (LOGGING(LOG_OPT))
    {
      std::ostringstream str;
      str << " -- non-basic indices:";
      for (unsigned i=0; i< _nonbas.size(); i++)
        str << " " << _nonbas[i];
      FILE_LOG(LOG_OPT) << str.str() << std::endl;
    }
  }
  else
  {
    // get minimum element of q (really w)
    unsigned minw = std::min_element(q.begin(), q.end()) - q.begin();
    if (q[minw] > -zero_tol)
    {
      FILE_LOG(LOG_OPT) << "LCP::lcp_fast() - trivial solution found" << std::endl;
      z.set_zero(N);
      return true;
    }

    // setup basic and nonbasic variable indices
    _nonbas.push_back(minw); 
    _bas.resize(N-1);
    for (unsigned i=0, j=0; i< N; i++)
      if (i != minw)
        _bas[j++] = i;
  }

  // loop for maximum number of pivots
//  const unsigned MAX_PIV = std::max(N*N, (unsigned) 1000);
  const unsigned MAX_PIV = 2*N;
  for (pivots=0; pivots < MAX_PIV; pivots++)
  {
    // select nonbasic indices
    M.select_square(_nonbas.begin(), _nonbas.end(), _Msub);
    M.select(_bas.begin(), _bas.end(), _nonbas.begin(), _nonbas.end(), _Mmix);
    q.select(_nonbas.begin(), _nonbas.end(), _z);
    q.select(_bas.begin(), _bas.end(), _qbas);
    _z.negate();

    // solve for nonbasic z
    try
    {
      _LA.solve_fast(_Msub, _z);
    }
    catch (SingularException e)
    {
      FILE_LOG(LOG_OPT) << "LCP::lcp_fast() - linear system solve failed" << std::endl;
      return false;
    }

    // compute w and find minimum value
    _Mmix.mult(_z, _w) += _qbas;
    unsigned minw = (_w.rows() > 0) ? rand_min(_w, zero_tol) : UINF;

    if (LOGGING(LOG_OPT))
    {
      // found the solution
      z.set_zero(N);

      // set values of z corresponding to _z
      std::ostringstream nbas_str, bas_str;
      for (unsigned i=0, j=0; j < _nonbas.size(); i++, j++)
      {
        nbas_str << _nonbas[j] << " ";
        z[_nonbas[j]] = _z[i];
      }
      for (unsigned j=0; j< _bas.size(); j++)
        bas_str << _bas[j] << " ";

      VectorNd tmpv;
      M.mult(z, tmpv) += q; 
      FILE_LOG(LOG_OPT) << " non-basic indices: " << nbas_str.str() << std::endl;
      FILE_LOG(LOG_OPT) << " basic indices: " << bas_str.str() << std::endl;
      FILE_LOG(LOG_OPT) << " computed z: " << z << std::endl;
      FILE_LOG(LOG_OPT) << " new w: " << tmpv << std::endl;
    }
    if (minw < UINF)
      FILE_LOG(LOG_OPT) << "LCP::lcp_fast() - minimum w after pivot: " << _w[minw] << std::endl;

    // if w >= 0, check whether any component of z < 0
    if (minw == UINF || _w[minw] > -zero_tol)
    {
      // find the (a) minimum of z
      unsigned minz = (_z.rows() > 0) ? rand_min(_z, zero_tol) : UINF; 
      if (LOGGING(LOG_OPT) && _z.rows() > 0)
        FILE_LOG(LOG_OPT) << "LCP::lcp_fast() - minimum z after pivot: " << _z[minz] << std::endl;
      if (minz < UINF && _z[minz] < -zero_tol)
      {
        // get the original index and remove it from the nonbasic set
        unsigned idx = _nonbas[minz];
        _nonbas.erase(_nonbas.begin()+minz);
        
        // move index to basic set and continue looping
        _bas.push_back(idx);
        insertion_sort(_bas.begin(), _bas.end());
      }
      else
      {
        // found the solution
        z.set_zero(N);

        // set values of z corresponding to _z
        for (unsigned i=0, j=0; j < _nonbas.size(); i++, j++)
          z[_nonbas[j]] = _z[i];

        FILE_LOG(LOG_OPT) << "LCP::lcp_fast() - solution found!" << std::endl;
        return true;
      }
    }
    else
    {
      FILE_LOG(LOG_OPT) << "(minimum w too negative)" << std::endl;

      // one or more components of w violating w >= 0
      // prepare to move component of w from basic set to nonbasic set
      unsigned widx = _bas[minw];
      FILE_LOG(LOG_OPT) << "moving index " << widx << " to nonbasic set" << std::endl;

      // look whether any component of z needs to move to basic set
      unsigned minz = (_z.rows() > 0) ? rand_min(_z, zero_tol) : UINF; 
      if (LOGGING(LOG_OPT) && _z.rows() > 0)
        FILE_LOG(LOG_OPT) << "LCP::lcp_fast() - minimum z after pivot: " << _z[minz] << std::endl;
      if (minz < UINF &&_z[minz] < -zero_tol)
      {
        // prepare to move index to basic set and continue looping
        unsigned zidx = _nonbas[minz];
        FILE_LOG(LOG_OPT) << "LCP::lcp_fast() - moving index " << zidx << " to basic set" << std::endl;

        _nonbas.erase(_nonbas.begin()+minz);
        _nonbas.push_back(widx);
        _bas.erase(_bas.begin()+minw);
        _bas.push_back(zidx);
        insertion_sort(_bas.begin(), _bas.end());
        insertion_sort(_nonbas.begin(), _nonbas.end());
      }
      else
      {
        _bas.erase(_bas.begin()+minw);
        _nonbas.push_back(widx);
        insertion_sort(_nonbas.begin(), _nonbas.end());
      }
    }
  }

  FILE_LOG(LOG_OPT) << "LCP::lcp_fast() - maximum allowable pivots exceeded" << std::endl;

  // if we're here, then the maximum number of pivots has been exceeded
  return false;
}

/// Get the minimum index of vector v; if there are multiple minima (within zero_tol), returns one randomly 
unsigned LCP::rand_min(const VectorNd& v, double zero_tol)
{
  static vector<unsigned> minima;
  minima.clear();
  unsigned minv = std::min_element(v.begin(), v.end()) - v.begin();
  minima.push_back(minv);
  for (unsigned i=0; i< v.rows(); i++)
    if (i != minv && v[i] < v[minv] + zero_tol)
      minima.push_back(i);
  return minima[rand() % minima.size()];
}

/// Regularized wrapper around PPM I with bounds constraints 
bool LCP::mlcp_fast_regularized(const MatrixNd& M, const VectorNd& q, const VectorNd& l, const VectorNd& u, VectorNd& z, int min_exp, unsigned step_exp, int max_exp, double piv_tol, double zero_tol)
{
  FILE_LOG(LOG_OPT) << "LCP::mlcp_fast_regularized() entered" << endl;
  pair<ColumnIteratord, ColumnIteratord> mmax;
 
  // look for fast exit
  if (q.size() == 0)
  {
    z.resize(0);
    return true;
  }

  // copy MM
  _MM = M;

  // assign value for zero tolerance, if necessary
  const double ZERO_TOL = (zero_tol > (double) 0.0) ? zero_tol : q.size() * M.norm_inf() * NEAR_ZERO;

  FILE_LOG(LOG_OPT) << " zero tolerance: " << ZERO_TOL << endl;

  // store the total pivots
  unsigned total_piv = 0;

  // try non-regularized version first
  bool result = mlcp_fast(_MM, q, l, u, z, ZERO_TOL);
  if (result)
  {
    FILE_LOG(LOG_OPT) << "  LCP::mlcp_fast() reports that LCP solved to desired tolerance" << std::endl;

    // verify that solution truly is a solution -- first compute w 
    M.mult(z, _wx) += q;

    // now check each pair z/w
    bool found = true;
    for (unsigned i=0; i< z.size(); i++)
    {
      // look for z at lower bound
      if (z[i] < l[i] + ZERO_TOL)
      {
        // look for lower bound exceeded or negative w
        if (z[i] + ZERO_TOL < l[i] || _wx[i] < -ZERO_TOL)
        {
          found = false;
          break;
        }
      }
      // look for z at upper bound
      else if (z[i] > u[i] - ZERO_TOL)
      {
        // look for upper bound exceeded or positive w
        if (z[i] - ZERO_TOL > u[i] || _wx[i] > ZERO_TOL)
        {
          found = false;
          break;
        }
      }
      // z is not near a bound; w should be effectively zero
      else if (std::fabs(_wx[i]) > ZERO_TOL)
      {
        found = false;
        break;
      }
    } 

    if (found)
    {          
      FILE_LOG(LOG_OPT) << "  solved with no regularization necessary!" << endl;
      FILE_LOG(LOG_OPT) << "  pivots / total pivots: " << pivots << " " << pivots << endl;
      FILE_LOG(LOG_OPT) << "LCP::mlcp_fast_regularized() exited" << endl;
      return true;
    }
    else
      FILE_LOG(LOG_OPT) << "  LCP::mlcp_fast_regularized() - failed to solve w/o regularization" << std::endl;
  }

  // update the pivots
  total_piv += pivots;

  // start the regularization process
  int rf = min_exp;
  while (rf <= max_exp)
  {
    // setup regularization factor
    double lambda = std::pow((double) 10.0, (double) rf);

    FILE_LOG(LOG_OPT) << "  trying to solve LCP with regularization factor: " << lambda << endl;

    // regularize M
    _MM = M;
    for (unsigned i=0; i< M.rows(); i++)
      _MM(i,i) += lambda;

    // try to solve the LCP
    result = mlcp_fast(_MM, q, l, u, z, ZERO_TOL);

    // update total pivots
    total_piv += pivots;

    if (result)
    {
      FILE_LOG(LOG_OPT) << "  LCP::mlcp_fast() reports that LCP solved to desired tolerance" << std::endl;

      // verify that solution truly is a solution -- first compute w 
      _MM.mult(z, _wx) += q;

      // now check each pair z/w
      bool found = true;
      for (unsigned i=0; i< z.size(); i++)
      {
        // look for z at lower bound
        if (z[i] < l[i] + ZERO_TOL)
        {
          // look for lower bound exceeded or negative w
          if (z[i] + ZERO_TOL < l[i] || _wx[i] < -ZERO_TOL)
          {
            FILE_LOG(LOG_OPT) << "  invalid z/l (" << z[i] << "/" << l[i] << ") or w(" << _wx[i] <<")" << std::endl;
            found = false;
            if (!LOGGING(LOG_OPT))
              break;
          }
        }
        // look for z at upper bound
        else if (z[i] > u[i] - ZERO_TOL)
        {
          // look for upper bound exceeded or positive w
          if (z[i] - ZERO_TOL > u[i] || _wx[i] > ZERO_TOL)
          {
            FILE_LOG(LOG_OPT) << "  invalid z/u (" << z[i] << "/" << u[i] << ") or w(" << _wx[i] <<")" << std::endl;
            found = false;
            if (!LOGGING(LOG_OPT))
              break;
          }
        }
        // z is not near a bound; w should be effectively zero
        else if (std::fabs(_wx[i]) > ZERO_TOL)
        {
          FILE_LOG(LOG_OPT) << "  invalid complementarity, z=" << z[i] << ", w=" << _wx[i] <<std::endl;
          found = false;
          if (!LOGGING(LOG_OPT))
            break;
        }
      } 

      if (found)
      {          
        FILE_LOG(LOG_OPT) << "  solved with regularization!" << endl;
        FILE_LOG(LOG_OPT) << "  pivots / total pivots: " << pivots << " " << pivots << endl;
        FILE_LOG(LOG_OPT) << "LCP::mlcp_fast_regularized() exited" << endl;
        return true;
      }
      else
        FILE_LOG(LOG_OPT) << "  LCP::mlcp_fast_regularized() - failed to solve w/ current regularization factor" << std::endl;
    }

    // increase rf
    rf += step_exp;
  }

  FILE_LOG(LOG_OPT) << "  unable to solve given any regularization!" << endl;
  FILE_LOG(LOG_OPT) << "LCP::mlcp_fast_regularized() exited" << endl;

  // store total pivots
  pivots = total_piv;

  // still here?  failure...
  return false;
}


/// Regularized wrapper around PPM I with bounds constraints 
bool LCP::mlcp_keller_regularized(const MatrixNd& M, const VectorNd& q, const VectorNd& l, const VectorNd& u, VectorNd& z, int min_exp, unsigned step_exp, int max_exp, double piv_tol, double zero_tol)
{
  FILE_LOG(LOG_OPT) << "LCP::mlcp_keller_regularized() entered" << endl;
  pair<ColumnIteratord, ColumnIteratord> mmax;
 
  // look for fast exit
  if (q.size() == 0)
  {
    z.resize(0);
    return true;
  }

  // copy MM
  _MM = M;

  // assign value for zero tolerance, if necessary
  const double ZERO_TOL = (zero_tol > (double) 0.0) ? zero_tol : q.size() * M.norm_inf() * NEAR_ZERO;

  FILE_LOG(LOG_OPT) << " zero tolerance: " << ZERO_TOL << endl;

  // store the total pivots
  unsigned total_piv = 0;

  // try non-regularized version first
  bool result = mlcp_keller(_MM, q, l, u, z, ZERO_TOL);
  if (result)
  {
    FILE_LOG(LOG_OPT) << "  LCP::mlcp_keller() reports that LCP solved to desired tolerance" << std::endl;

    // verify that solution truly is a solution -- first compute w 
    M.mult(z, _wx) += q;

    // now check each pair z/w
    bool found = true;
    for (unsigned i=0; i< z.size(); i++)
    {
      // look for z at lower bound
      if (z[i] < l[i] + ZERO_TOL)
      {
        // look for lower bound exceeded or negative w
        if (z[i] + ZERO_TOL < l[i] || _wx[i] < -ZERO_TOL)
        {
          found = false;
          break;
        }
      }
      // look for z at upper bound
      else if (z[i] > u[i] - ZERO_TOL)
      {
        // look for upper bound exceeded or positive w
        if (z[i] - ZERO_TOL > u[i] || _wx[i] > ZERO_TOL)
        {
          found = false;
          break;
        }
      }
      // z is not near a bound; w should be effectively zero
      else if (std::fabs(_wx[i]) > ZERO_TOL)
      {
        found = false;
        break;
      }
    } 

    if (found)
    {          
      FILE_LOG(LOG_OPT) << "  solved with no regularization necessary!" << endl;
      FILE_LOG(LOG_OPT) << "  pivots / total pivots: " << pivots << " " << pivots << endl;
      FILE_LOG(LOG_OPT) << "LCP::mlcp_keller_regularized() exited" << endl;
      return true;
    }
    else
      FILE_LOG(LOG_OPT) << "  LCP::mlcp_keller_regularized() - failed to solve w/o regularization" << std::endl;
  }

  // update the pivots
  total_piv += pivots;

  // start the regularization process
  int rf = min_exp;
  while (rf <= max_exp)
  {
    // setup regularization factor
    double lambda = std::pow((double) 10.0, (double) rf);

    FILE_LOG(LOG_OPT) << "  trying to solve LCP with regularization factor: " << lambda << endl;

    // regularize M
    _MM = M;
    for (unsigned i=0; i< M.rows(); i++)
      _MM(i,i) += lambda;

    // try to solve the LCP
    result = mlcp_keller(_MM, q, l, u, z, ZERO_TOL);

    // update total pivots
    total_piv += pivots;

    if (result)
    {
      FILE_LOG(LOG_OPT) << "  LCP::mlcp_keller() reports that LCP solved to desired tolerance" << std::endl;

      // verify that solution truly is a solution -- first compute w 
      _MM.mult(z, _wx) += q;

      // now check each pair z/w
      bool found = true;
      for (unsigned i=0; i< z.size(); i++)
      {
        // look for z at lower bound
        if (z[i] < l[i] + ZERO_TOL)
        {
          // look for lower bound exceeded or negative w
          if (z[i] + ZERO_TOL < l[i] || _wx[i] < -ZERO_TOL)
          {
            FILE_LOG(LOG_OPT) << "  invalid z/l (" << z[i] << "/" << l[i] << ") or w(" << _wx[i] <<")" << std::endl;
            found = false;
            if (!LOGGING(LOG_OPT))
              break;
          }
        }
        // look for z at upper bound
        else if (z[i] > u[i] - ZERO_TOL)
        {
          // look for upper bound exceeded or positive w
          if (z[i] - ZERO_TOL > u[i] || _wx[i] > ZERO_TOL)
          {
            FILE_LOG(LOG_OPT) << "  invalid z/u (" << z[i] << "/" << u[i] << ") or w(" << _wx[i] <<")" << std::endl;
            found = false;
            if (!LOGGING(LOG_OPT))
              break;
          }
        }
        // z is not near a bound; w should be effectively zero
        else if (std::fabs(_wx[i]) > ZERO_TOL)
        {
          FILE_LOG(LOG_OPT) << "  invalid complementarity, z=" << z[i] << ", w=" << _wx[i] <<std::endl;
          found = false;
          if (!LOGGING(LOG_OPT))
            break;
        }
      } 

      if (found)
      {          
        FILE_LOG(LOG_OPT) << "  solved with regularization!" << endl;
        FILE_LOG(LOG_OPT) << "  pivots / total pivots: " << pivots << " " << pivots << endl;
        FILE_LOG(LOG_OPT) << "LCP::mlcp_keller_regularized() exited" << endl;
        return true;
      }
      else
        FILE_LOG(LOG_OPT) << "  LCP::mlcp_keller_regularized() - failed to solve w/ current regularization factor" << std::endl;
    }

    // increase rf
    rf += step_exp;
  }

  FILE_LOG(LOG_OPT) << "  unable to solve given any regularization!" << endl;
  FILE_LOG(LOG_OPT) << "LCP::mlcp_keller_regularized() exited" << endl;

  // store total pivots
  pivots = total_piv;

  // still here?  failure...
  return false;
}

/// Regularized wrapper around PPM I 
bool LCP::lcp_fast_regularized(const MatrixNd& M, const VectorNd& q, VectorNd& z, int min_exp, unsigned step_exp, int max_exp, double piv_tol, double zero_tol)
{
  FILE_LOG(LOG_OPT) << "LCP::lcp_fast_regularized() entered" << endl;
  pair<ColumnIteratord, ColumnIteratord> mmax;
 
  // look for fast exit
  if (q.size() == 0)
  {
    z.resize(0);
    return true;
  }

  // copy MM
  _MM = M;

  // assign value for zero tolerance, if necessary
  const double ZERO_TOL = (zero_tol > (double) 0.0) ? zero_tol : q.size() * M.norm_inf() * NEAR_ZERO;

  FILE_LOG(LOG_OPT) << " zero tolerance: " << ZERO_TOL << endl;

  // store the total pivots
  unsigned total_piv = 0;

  // try non-regularized version first
  bool result = lcp_fast(_MM, q, z, zero_tol);
  if (result)
  {
    // verify that solution truly is a solution -- check z
    if (*std::min_element(z.begin(), z.end()) >= -ZERO_TOL)
    {
      // check w
      M.mult(z, _wx) += q;
      if (*std::min_element(_wx.begin(), _wx.end()) >= -ZERO_TOL)
      {
        // check z'w
        std::transform(z.begin(), z.end(), _wx.begin(), _wx.begin(), std::multiplies<double>());
        mmax = boost::minmax_element(_wx.begin(), _wx.end());
        if (*mmax.first >= -ZERO_TOL && *mmax.second < ZERO_TOL)
        {
          FILE_LOG(LOG_OPT) << "  solved with no regularization necessary!" << endl;
          FILE_LOG(LOG_OPT) << "  pivots / total pivots: " << pivots << " " << pivots << endl;
          FILE_LOG(LOG_OPT) << "LCP::lcp_fast_regularized() exited" << endl;

          return true;
        }
        else
        {
          FILE_LOG(LOG_OPT) << "LCP::lcp_fast_regularized() - '<w, z> not within tolerance(min value: " << *mmax.first << " max value: " << *mmax.second << ")" << std::endl; 
        }
      }
      else
      {
        FILE_LOG(LOG_OPT) << "  LCP::lcp_fast_regularized() - 'w' not solved to desired tolerance" << std::endl;
        FILE_LOG(LOG_OPT) << "  minimum w: " << *std::min_element(_wx.column_iterator_begin(), _wx.column_iterator_end()) << std::endl;
      }
    }
    else
    {
      FILE_LOG(LOG_OPT) << "  LCP::lcp_fast_regularized() - 'z' not solved to desired tolerance" << std::endl;
      FILE_LOG(LOG_OPT) << "  minimum z: " << *std::min_element(z.column_iterator_begin(), z.column_iterator_end()) << std::endl;
    }
  }
  else
    FILE_LOG(LOG_OPT) << "  LCP::lcp_fast_regularized() - solver failed with zero regularization" << std::endl;

  // update the pivots
  total_piv += pivots;

  // start the regularization process
  int rf = min_exp;
  while (rf <= max_exp)
  {
    // setup regularization factor
    double lambda = std::pow((double) 10.0, (double) rf);

    FILE_LOG(LOG_OPT) << "  trying to solve LCP with regularization factor: " << lambda << endl;

    // regularize M
    _MM = M;
    for (unsigned i=0; i< M.rows(); i++)
      _MM(i,i) += lambda;

    // try to solve the LCP
    result = lcp_fast(_MM, q, z, zero_tol);

    // update total pivots
    total_piv += pivots;

    if (result)
    {
      // verify that solution truly is a solution -- check z
      if (*std::min_element(z.begin(), z.end()) > -ZERO_TOL)
      {
        // check w
        _MM.mult(z, _wx) += q;
        if (*std::min_element(_wx.begin(), _wx.end()) > -ZERO_TOL)
        {
          // check z'w
          std::transform(z.begin(), z.end(), _wx.begin(), _wx.begin(), std::multiplies<double>());
          mmax = boost::minmax_element(_wx.begin(), _wx.end());
          if (*mmax.first > -ZERO_TOL && *mmax.second < ZERO_TOL)
          {
            FILE_LOG(LOG_OPT) << "  solved with regularization factor: " << lambda << endl;
            FILE_LOG(LOG_OPT) << "  pivots / total pivots: " << pivots << " " << total_piv << endl;
            FILE_LOG(LOG_OPT) << "LCP::lcp_fast_regularized() exited" << endl;
            pivots = total_piv;
            return true;
          }
          else
          {
            FILE_LOG(LOG_OPT) << "LCP::lcp_fast_regularized() - '<w, z> not within tolerance(min value: " << *mmax.first << " max value: " << *mmax.second << ")" << std::endl; 
          }
        }
        else
        {
          FILE_LOG(LOG_OPT) << "  LCP::lcp_fast_regularized() - 'w' not solved to desired tolerance" << std::endl;
          FILE_LOG(LOG_OPT) << "  minimum w: " << *std::min_element(_wx.column_iterator_begin(), _wx.column_iterator_end()) << std::endl;
        }
      }
      else
      {
        FILE_LOG(LOG_OPT) << "  LCP::lcp_fast_regularized() - 'z' not solved to desired tolerance" << std::endl;
        FILE_LOG(LOG_OPT) << "  minimum z: " << *std::min_element(z.column_iterator_begin(), z.column_iterator_end()) << std::endl;
      }
    }

    // increase rf
    rf += step_exp;
  }

  FILE_LOG(LOG_OPT) << "  unable to solve given any regularization!" << endl;
  FILE_LOG(LOG_OPT) << "LCP::lcp_fast_regularized() exited" << endl;

  // store total pivots
  pivots = total_piv;

  // still here?  failure...
  return false;
}

/// Regularized wrapper around Lemke's algorithm
bool LCP::lcp_lemke_regularized(const MatrixNd& M, const VectorNd& q, VectorNd& z, int min_exp, unsigned step_exp, int max_exp, double piv_tol, double zero_tol)
{
  FILE_LOG(LOG_OPT) << "LCP::lcp_lemke_regularized() entered" << endl;
  pair<ColumnIteratord, ColumnIteratord> mmax;
 
  // look for fast exit
  if (q.size() == 0)
  {
    z.resize(0);
    return true;
  }

  // copy MM
  _MM = M;

  // assign value for zero tolerance, if necessary
  const double ZERO_TOL = (zero_tol > (double) 0.0) ? zero_tol : q.size() * M.norm_inf() * NEAR_ZERO;

  FILE_LOG(LOG_OPT) << " zero tolerance: " << ZERO_TOL << endl;

  // store the total pivots
  unsigned total_piv = 0;

  // try non-regularized version first
  bool result = lcp_lemke(_MM, q, z, piv_tol, zero_tol);
  if (result)
  {
    // verify that solution truly is a solution -- check z
    if (*std::min_element(z.begin(), z.end()) >= -ZERO_TOL)
    {
      // check w
      M.mult(z, _wx) += q;
      if (*std::min_element(_wx.begin(), _wx.end()) >= -ZERO_TOL)
      {
        // check z'w
        std::transform(z.begin(), z.end(), _wx.begin(), _wx.begin(), std::multiplies<double>());
        mmax = boost::minmax_element(_wx.begin(), _wx.end());
        if (*mmax.first >= -ZERO_TOL && *mmax.second < ZERO_TOL)
        {
          FILE_LOG(LOG_OPT) << "  solved with no regularization necessary!" << endl;
          FILE_LOG(LOG_OPT) << "LCP::lcp_lemke_regularized() exited" << endl;

          return true;
        }
        else
        {
          FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() - '<w, z> not within tolerance(min value: " << *mmax.first << " max value: " << *mmax.second << ")" << std::endl; 
        }
      }
      else
      {
        FILE_LOG(LOG_OPT) << "  LCP::lcp_lemke() - 'w' not solved to desired tolerance" << std::endl;
        FILE_LOG(LOG_OPT) << "  minimum w: " << *std::min_element(_wx.column_iterator_begin(), _wx.column_iterator_end()) << std::endl;
      }
    }
    else
    {
      FILE_LOG(LOG_OPT) << "  LCP::lcp_lemke() - 'z' not solved to desired tolerance" << std::endl;
      FILE_LOG(LOG_OPT) << "  minimum z: " << *std::min_element(z.column_iterator_begin(), z.column_iterator_end()) << std::endl;
    }
  }

  // update the pivots
  total_piv += pivots;

  // start the regularization process
  int rf = min_exp;
  while (rf <= max_exp)
  {
    // setup regularization factor
    double lambda = std::pow((double) 10.0, (double) rf);

    FILE_LOG(LOG_OPT) << "  trying to solve LCP with regularization factor: " << lambda << endl;

    // regularize M
    _MM = M;
    for (unsigned i=0; i< M.rows(); i++)
      _MM(i,i) += lambda;

    // try to solve the LCP
    result = lcp_lemke(_MM, q, z, piv_tol, zero_tol);

    // update total pivots
    total_piv += pivots;

    if (result)
    {
      // verify that solution truly is a solution -- check z
      if (*std::min_element(z.begin(), z.end()) > -ZERO_TOL)
      {
        // check w
        _MM.mult(z, _wx) += q;
        if (*std::min_element(_wx.begin(), _wx.end()) > -ZERO_TOL)
        {
          // check z'w
          std::transform(z.begin(), z.end(), _wx.begin(), _wx.begin(), std::multiplies<double>());
          mmax = boost::minmax_element(_wx.begin(), _wx.end());
          if (*mmax.first > -ZERO_TOL && *mmax.second < ZERO_TOL)
          {
            FILE_LOG(LOG_OPT) << "  solved with regularization factor: " << lambda << endl;
            FILE_LOG(LOG_OPT) << "LCP::lcp_lemke_regularized() exited" << endl;
            pivots = total_piv;
            return true;
          }
          else
          {
            FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() - '<w, z> not within tolerance(min value: " << *mmax.first << " max value: " << *mmax.second << ")" << std::endl; 
          }
        }
        else
        {
          FILE_LOG(LOG_OPT) << "  LCP::lcp_lemke() - 'w' not solved to desired tolerance" << std::endl;
          FILE_LOG(LOG_OPT) << "  minimum w: " << *std::min_element(_wx.column_iterator_begin(), _wx.column_iterator_end()) << std::endl;
        }
      }
      else
      {
        FILE_LOG(LOG_OPT) << "  LCP::lcp_lemke() - 'z' not solved to desired tolerance" << std::endl;
        FILE_LOG(LOG_OPT) << "  minimum z: " << *std::min_element(z.column_iterator_begin(), z.column_iterator_end()) << std::endl;
      }
    }

    // increase rf
    rf += step_exp;
  }

  FILE_LOG(LOG_OPT) << "  unable to solve given any regularization!" << endl;
  FILE_LOG(LOG_OPT) << "LCP::lcp_lemke_regularized() exited" << endl;

  // store total pivots
  pivots = total_piv;

  // still here?  failure...
  return false;
}

/// Sets a basis
void LCP::set_basis(unsigned n, unsigned count, vector<unsigned>& bas, vector<unsigned>& nbas)
{
  // clear bas and nbas
  bas.clear();
  nbas.clear();

  unsigned long long countL = count;
//  unsigned long long n2 = 1L << ((unsigned long long) n-1);
  unsigned long long n2 = 1;
  for (unsigned i=0; i< n; i++)
    n2 *= 2;

  for (unsigned i=0; i< n; i++)
  {
    if (countL / n2 > 0)
      bas.push_back(i);
    else
      nbas.push_back(i);
    countL = countL % n2;
    n2 = n2 >> 1L;
    assert(n2 != 0);
  } 
}

/// Logs LCP solver failure
void LCP::log_failure(const MatrixNd& M, const VectorNd& q)
{
  // generate a unique filename
  std::ostringstream fname;
  fname << "lemke.Mq.";
  for (unsigned i=0; i< 8; i++)
    fname << rand() % 10;
  fname << ".fail";

  // open the file
  std::ofstream out(fname.str().c_str());

  // write the matrix
  for (unsigned i=0; i< M.rows(); i++)
  {
    for (unsigned j=0; j< M.columns(); j++)
      out << M(i,j) << " ";
    out << std::endl;
  }
  for (unsigned j=0; j< M.columns(); j++)
    out << q[j] << " ";
  out << std::endl;
  out.close();
}

/// Lemke's algorithm for solving linear complementarity problems
/**
 * \param z a vector "close" to the solution on input (optional); contains
 *        the solution on output
 */
bool LCP::lcp_lemke(const MatrixNd& M, const VectorNd& q, VectorNd& z, double piv_tol, double zero_tol)
{
  const unsigned n = q.size();
  const unsigned MAXITER = std::min((unsigned) 1000, 50*n);

  // indicate whether we've restarted
  bool restarted = false;

  // update the pivots
  pivots = 0;

  // look for immediate exit
  if (n == 0)
  {
    z.resize(0);
    return true;
  }

  // Lemke's algorithm doesn't seem to like warmstarting
  z.set_zero();

  // copy z to z0
  _z0 = z;

  // come up with a sensible value for zero tolerance if none is given
  if (zero_tol <= (double) 0.0)
    zero_tol = std::numeric_limits<double>::epsilon() * M.norm_inf() * n;

  FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() entered" << endl;
  FILE_LOG(LOG_OPT) << "  M: " << endl << M;
  FILE_LOG(LOG_OPT) << "  q: " << q << endl;

  // see whether trivial solution exists
  if (*std::min_element(q.begin(), q.end()) > -zero_tol)
  {
    FILE_LOG(LOG_OPT) << " -- trivial solution found" << endl;
    FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() exited" << endl;
    z.set_zero(n);
    return true;
  }

restart: // solver restarts from here when basis becomes bad

  // clear all vectors
  _all.clear();
  _tlist.clear();
  _bas.clear();
  _nonbas.clear();
  _j.clear();

  // initialize variables
  z.set_zero(n*2);
  unsigned t = 2*n;
  unsigned entering = t;
  unsigned leaving = 0;
  _all.clear();
  for (unsigned i=0; i< n; i++)
    _all.push_back(i);
  unsigned lvindex;
  unsigned idx;
  vector<unsigned>::iterator iiter;
  _tlist.clear();

  // determine initial basis
  _bas.clear();
  _nonbas.clear();
  if (_z0.size() != n)
  {
    // setup the nonbasic indices
    for (unsigned i=0; i< n; i++)
      _nonbas.push_back(i);

    // set the restart basis to random
    _restart_z0.resize(n);
    for (unsigned i=0; i< n; i++)
      _restart_z0[i] = (rand() % 2 == 0) ? 0.0 : 1.0;
  }
  else
  {
    // setup the initial basis
    for (unsigned i=0; i< n; i++)
      if (_z0[i] > 0)
        _bas.push_back(i);
      else
        _nonbas.push_back(i);

    // setup the restart basis
    if (!restarted)
      _restart_z0.set_zero(n);
    else
    {
      FILE_LOG(LOG_OPT) << "-- setting restart basis to random" << std::endl;

      // we've already restarted once, set the restart basis to random
      _restart_z0.resize(n);
      for (unsigned i=0; i< n; i++)
        _restart_z0[i] = (rand() % 2 == 0) ? 0.0 : 1.0;
    }
  }

  // determine initial values
  if (!_bas.empty())
  {
      FILE_LOG(LOG_OPT) << "-- initial basis not empty (warmstarting)" << std::endl;

    // start from good initial basis 
    _Bl.set_identity(n);
    _Bl.negate();

    // select columns of M corresponding to z vars in the basis
    M.select(_all.begin(), _all.end(), _bas.begin(), _bas.end(), _t1);

    // select columns of I corresponding to z vars not in the basis
    _Bl.select(_all.begin(), _all.end(), _nonbas.begin(), _nonbas.end(), _t2);

    // setup the basis matrix
    _Bl.resize(n, _t1.columns() + _t2.columns());
    _Bl.set_sub_mat(0,0,_t1);
    _Bl.set_sub_mat(0,_t1.columns(),_t2);

    // solve B*x = -q
    try
    {
      _Al = _Bl;
      _x = q;
      _LA.solve_fast(_Al, _x);
    }
    catch (SingularException e)
    {
      // initial basis was no good, set it up as if we have no basis
      _bas.clear();
      _nonbas.clear();
      for (unsigned i=0; i< n; i++)
        _nonbas.push_back(i);

      // set B to -1 and solve x correspondingly 
      _Bl.set_identity(n);
      _Bl.negate();
      _x = q;

      // set next initial basis to random
      _restart_z0.resize(n);
      for (unsigned i=0; i< n; i++)
        _restart_z0[i] = (rand() % 2 == 0) ? 0.0 : 1.0;
    }
  }
  else
  {
    FILE_LOG(LOG_OPT) << "-- using basis of -1 (no warmstarting)" << std::endl;

    // use standard initial basis
    _Bl.set_identity(n);
    _Bl.negate();
    _x = q;
  }

/*
  unsigned basis_count = std::numeric_limits<unsigned>::max();
  while (true)
  {
    try
    {
      _Al = _Bl;
      _x = q;
      _LA.solve_fast(_Al, _x);
      break;
    }
    catch (SingularException e)
    {
      // if initial basis didn't work, prepare to iterate through all 2^n bases
      if (basis_count == std::numeric_limits<unsigned>::max())
        basis_count = 0;
        
      // cycle through all bases until we find one that works
      set_basis(n, basis_count++, _bas, _nonbas);

      // select columns of M corresponding to z vars in the basis
      M.select(_all.begin(), _all.end(), _bas.begin(), _bas.end(), _t1);

      // select columns of I corresponding to z vars not in the basis
      _Bl.select(_all.begin(), _all.end(), _nonbas.begin(), _nonbas.end(), _t2);

      // setup the basis matrix
      _Bl.resize(n, _t1.columns() + _t2.columns());
      _Bl.set_sub_mat(0,0,_t1);
      _Bl.set_sub_mat(0,_t1.columns(),_t2);
    }
  }
  _x.negate();
*/

  // check whether initial basis provides a solution
  if (std::find_if(_x.begin(), _x.end(), std::bind2nd(std::less<double>(), 0.0)) == _x.end())
  {
    for (idx = 0, iiter = _bas.begin(); iiter != _bas.end(); iiter++, idx++)
      z[*iiter] = _x[idx];
    z.resize(n, true);

    // check to see whether tolerances are satisfied
    FILE_LOG(LOG_OPT) << " -- initial basis provides a solution!" << std::endl;
    if (LOGGING(LOG_OPT))
    {
      M.mult(z, _wl) += q;
      double minw = *std::min_element(_wl.begin(), _wl.end());
      double w_dot_z = std::fabs(_wl.dot(z));
      FILE_LOG(LOG_OPT) << "  z: " << z << std::endl;
      FILE_LOG(LOG_OPT) << "  _w: " << _wl << std::endl;
      FILE_LOG(LOG_OPT) << "  minimum w: " << minw << std::endl;
      FILE_LOG(LOG_OPT) << "  w'z: " << w_dot_z << std::endl;
    }
    FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() exited" << endl;

    return true; 
  }

  // use a new pivot tolerance if necessary
  const double PIV_TOL = (piv_tol > (double) 0.0) ? piv_tol : std::numeric_limits<double>::epsilon() * n * std::max((double) 1.0, M.norm_inf());

  // determine initial leaving variable
  ColumnIteratord min_x = std::min_element(_x.begin(), _x.begin() + n);
  double tval = -*min_x;
  BOOST_FOREACH(unsigned i, _nonbas) // add w variables to basis
    _bas.push_back(i+n);
  lvindex = std::distance(_x.begin(), min_x);
  iiter = _bas.begin();
  std::advance(iiter, lvindex);
  leaving = *iiter;
  FILE_LOG(LOG_OPT) << " -- x: " << _x << endl;
  FILE_LOG(LOG_OPT) << " -- first pivot: leaving index=" << lvindex << "  entering index=" << entering << " minimum value: " << tval << endl;

  // pivot in the artificial variable
  *iiter = t;    // replace w var with _z0 in basic indices
  _u.resize(n);
  for (unsigned i=0; i< n; i++)
    _u[i] = (_x[i] < 0.0) ? 1.0 : 0.0;
  _Bl.mult(_u, _Be);
  _Be.negate();
  _u *= tval;
  _x += _u;
  _x[lvindex] = tval;
  _Bl.set_column(lvindex, _Be);
  FILE_LOG(LOG_OPT) << "  new q: " << _x << endl;

  // main iterations begin here
  for (pivots=0; pivots< MAXITER; pivots++)
  {
    if (LOGGING(LOG_OPT))
    {
      std::ostringstream basic;
      for (unsigned i=0; i< _bas.size(); i++)
        basic << " " << _bas[i];
      FILE_LOG(LOG_OPT) << "basic variables:" << basic.str() << std::endl;
    }

    // check whether done; if not, get new entering variable
    if (leaving == t)
    {
      FILE_LOG(LOG_OPT) << "-- solved LCP successfully!" << endl;
      unsigned idx;
      for (idx = 0, iiter = _bas.begin(); iiter != _bas.end(); iiter++, idx++)
        z[*iiter] = _x[idx];
      z.resize(n, true);

      // verify tolerances
      if (LOGGING(LOG_OPT))
      {
        M.mult(z, _wl) += q;
        double minw = *std::min_element(_wl.begin(), _wl.end());
        double w_dot_z = std::fabs(_wl.dot(z));
        FILE_LOG(LOG_OPT) << "  found solution!" << std::endl;
        FILE_LOG(LOG_OPT) << "  minimum w: " << minw << std::endl;
        FILE_LOG(LOG_OPT) << "  w'z: " << w_dot_z << std::endl;
        FILE_LOG(LOG_OPT) << "  n: " << n << " number of pivots: " << pivots << std::endl;
      }
      FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() exited" << endl;

      return true; 
    }
    else if (leaving < n)
    {
      entering = n + leaving;
      _Be.set_zero(n);
      _Be[leaving] = -1;
    }
    else
    {
      entering = leaving - n;
      M.get_column(entering, _Be);
    }
    _dl = _Be;
    try
    {
      _Al = _Bl;
      _LA.solve_fast(_Al, _dl);
    }
    catch (SingularException e)
    {
      FILE_LOG(LOG_OPT) << " -- warning: linear system solver failed (basis became singular)" << std::endl;
      FILE_LOG(LOG_OPT) << " -- LCP::lcp_lemke() exiting" << std::endl;

      // log failure
      #ifndef NDEBUG 
//      log_failure(M, q);
      #endif 

      return false;
/*
      FILE_LOG(LOG_OPT) << " -- warning: linear system solver failed; restarting with new basis" << std::endl;

      // set the bases
      _z0 = _restart_z0;

      // setup the restart basis
      _restart_z0.resize(n); 
      for (unsigned i=0; i< n; i++)
        _restart_z0[i] = (rand() % 2 == 0) ? 1.0 : 0.0;

      // indicate we've restarted
      restarted = true;

      // restart
      goto restart;
*/
/*
// least squares solver
      try
      {
        // use slower SVD pseudo-inverse
        _Al = _Bl;
        _dl = _Be;
        _LA.solve_LS_fast1(_Al, _dl);
      }
      catch (NumericalException e)
      {
        _Al = _Bl;
        _LA.solve_LS_fast2(_Al, _dl);
      }
*/
    }

    // ** find new leaving variable
    _j.clear();
    for (unsigned i=0; i< _dl.size(); i++)
      if (_dl[i] > PIV_TOL)
        _j.push_back(i);

    // check for no new pivots; ray termination
    if (_j.empty())
    {
      FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() - no new pivots (ray termination)" << endl;
      FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() exiting" << endl;

      // log failure
      #ifndef NDEBUG 
//      log_failure(M, q);
      #endif

      return false;
    }

    if (LOGGING(LOG_OPT))
    {
      std::ostringstream j;
      for (unsigned i=0; i< _j.size(); i++)
        j << " " << _j[i];
      FILE_LOG(LOG_OPT) << "d: " << _dl << std::endl;
      FILE_LOG(LOG_OPT) << "j (before min ratio):" << j.str() << std::endl;
    }

    // select elements j from x and d
    _xj.resize(_j.size());
    _dj.resize(_xj.size());
    select(_x.begin(), _j.begin(), _j.end(), _xj.begin());
    select(_dl.begin(), _j.begin(), _j.end(), _dj.begin());

    // compute minimal ratios x(j) + EPS_DOUBLE ./ d(j), d > 0
    _result.set_zero(_xj.size());
    std::transform(_xj.begin(), _xj.end(), _result.begin(), std::bind2nd(std::plus<double>(), zero_tol));
    std::transform(_result.begin(), _result.end(), _dj.begin(), _result.begin(), std::divides<double>());
    double theta = *std::min_element(_result.begin(), _result.end());

    // NOTE: lexicographic ordering does not appear to be used here to prevent
    // cycling (see [Cottle 1992], pp. 340-342)
    // find indices of minimal ratios, d> 0
    //   divide _x(j) ./ d(j) -- remove elements above the minimum ratio
    std::transform(_xj.begin(), _xj.end(), _dj.begin(), _result.begin(), std::divides<double>());
    for (iiter = _j.begin(), idx = 0; iiter != _j.end(); )
      if (_result[idx++] <= theta)
        iiter++;
      else
        iiter = _j.erase(iiter);

    if (LOGGING(LOG_OPT))
    {
      std::ostringstream j;
      for (unsigned i=0; i< _j.size(); i++)
        j << " " << _j[i];
      FILE_LOG(LOG_OPT) << "j (after min ratio):" << j.str() << std::endl;
    }

    // if j is empty, then likely the zero tolerance is too low
    if (_j.empty())
    {
      FILE_LOG(LOG_OPT) << "zero tolerance too low?" << std::endl;
      FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() exited" << std::endl;
      z.resize(n, true);

      // log failure
      #ifndef NDEBUG 
//      log_failure(M, q);
      #endif

      return false;
    }

    // check whether artificial index among these
    _tlist.clear();
    select(_bas.begin(), _j.begin(), _j.end(), std::back_inserter(_tlist));
    if (std::find(_tlist.begin(), _tlist.end(), t) != _tlist.end())
    {
      iiter = std::find(_bas.begin(), _bas.end(), t); 
      lvindex = iiter - _bas.begin();
    }
    else
    {
      // several indices pass the minimum ratio test, pick one randomly
//      lvindex = _j[rand() % _j.size()];
// NOTE: solver seems *much* more capable of solving when we pick the first
// element rather than picking a random one
      lvindex = _j[0];
    }

    // set leaving = bas(lvindex)
    iiter = _bas.begin();
    std::advance(iiter, lvindex);
    leaving = *iiter;

    // ** perform pivot
    double ratio = _x[lvindex]/_dl[lvindex];
    _dl *= ratio;
    _x -= _dl;
    _x[lvindex] = ratio;
    _Bl.set_column(lvindex, _Be);
    *iiter = entering;
    FILE_LOG(LOG_OPT) << " -- pivoting: leaving index=" << lvindex << "  entering index=" << entering << endl;
  }

  FILE_LOG(LOG_OPT) << " -- maximum number of iterations exceeded (n=" << n << ", max=" << MAXITER << ")" << endl;
  FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() exited" << std::endl;

  // max iterations exceeded
  z.resize(n, true);
  // log failure
  #ifndef NDEBUG 
//  log_failure(M, q);
  #endif
 
  return false;
}

/// Regularized wrapper around Lemke's algorithm for srpase matrices
bool LCP::lcp_lemke_regularized(const SparseMatrixNd& M, const VectorNd& q, VectorNd& z, int min_exp, unsigned step_exp, int max_exp, double piv_tol, double zero_tol)
{
  FILE_LOG(LOG_OPT) << "LCP::lcp_lemke_regularized() entered" << endl;

  // look for fast exit
  if (q.size() == 0)
  {
    z.resize(0);
    return true;
  }

  // copy MM
  _MMs = M;

  // assign value for zero tolerance, if necessary
  const double ZERO_TOL = (zero_tol > (double) 0.0) ? zero_tol : q.size() * std::numeric_limits<double>::epsilon();

  // try non-regularized version first
  bool result = lcp_lemke(_MMs, q, z, piv_tol, zero_tol);
  if (result)
  {
    // verify that solution truly is a solution -- check z
    if (*std::min_element(z.begin(), z.end()) >= -ZERO_TOL)
    {
      // check w
      M.mult(z, _wx) += q;
      if (*std::min_element(_wx.begin(), _wx.end()) >= -ZERO_TOL)
      {
        // check z'w
        std::transform(z.begin(), z.end(), _wx.begin(), _wx.begin(), std::multiplies<double>());
        pair<ColumnIteratord, ColumnIteratord> mmax = boost::minmax_element(_wx.begin(), _wx.end());
        if (*mmax.first >= -ZERO_TOL && *mmax.second < ZERO_TOL)
        {
          FILE_LOG(LOG_OPT) << "  solved with no regularization necessary!" << endl;
          FILE_LOG(LOG_OPT) << "LCP::lcp_lemke_regularized() exited" << endl;
          return true;
        }
      }
    }
  }

  // add a zero sparse diagonal matrix to _MMs
  _eye = SparseMatrixNd::identity(q.size());
  (_zero = _eye) *= 0.0;
  _MMs += _zero; 

  // start the regularization process
  int rf = min_exp;
  while (rf <= max_exp)
  {
    // setup regularization factor
    double lambda = std::pow((double) 10.0, (double) rf);
    (_diag_lambda = _eye) *= lambda;

    // regularize M
    (_MMx = _MMs) += _diag_lambda;

    // try to solve the LCP
    if ((result = lcp_lemke(_MMx, q, z, piv_tol, zero_tol)))
    {
      // verify that solution truly is a solution -- check z
      if (*std::min_element(z.begin(), z.end()) > -ZERO_TOL)
      {
        // check w
        _MMx.mult(z, _wx) += q;
        if (*std::min_element(_wx.begin(), _wx.end()) > -ZERO_TOL)
        {
          // check z'w
          std::transform(z.begin(), z.end(), _wx.begin(), _wx.begin(), std::multiplies<double>());
          pair<ColumnIteratord, ColumnIteratord> mmax = boost::minmax_element(_wx.begin(), _wx.end());
          if (*mmax.first > -ZERO_TOL && *mmax.second < ZERO_TOL)
          {
            FILE_LOG(LOG_OPT) << "  solved with regularization factor: " << lambda << endl;
            FILE_LOG(LOG_OPT) << "LCP::lcp_lemke_regularized() exited" << endl;

            return true;
          }
        }
      }
    }

    // increase rf
    rf += step_exp;
  }

  FILE_LOG(LOG_OPT) << "  unable to solve given any regularization!" << endl;
  FILE_LOG(LOG_OPT) << "LCP::lcp_lemke_regularized() exited" << endl;

  // still here?  failure...
  return false;
}

/// Lemke's algorithm for solving linear complementarity problems using sparse matrices
/**
 * \param z a vector "close" to the solution on input (optional); contains
 *        the solution on output
 */
bool LCP::lcp_lemke(const SparseMatrixNd& M, const VectorNd& q, VectorNd& z, double piv_tol, double zero_tol)
{
  const unsigned n = q.size();
  const unsigned MAXITER = std::min((unsigned) 1000, 50*n);

  // look for immediate exit
  if (n == 0)
  {
    z.resize(0);
    return true;
  }

  // clear all vectors
  _all.clear();
  _tlist.clear();
  _bas.clear();
  _nonbas.clear();
  _j.clear();

  // copy z to z0
  _z0 = z;

  // come up with a sensible value for zero tolerance if none is given
  if (zero_tol <= (double) 0.0)
    zero_tol = std::numeric_limits<double>::epsilon() * M.norm_inf() * n;

  FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() entered" << endl;
  FILE_LOG(LOG_OPT) << "  M: " << endl << M;
  FILE_LOG(LOG_OPT) << "  q: " << q << endl;

  // see whether trivial solution exists
  if (*std::min_element(q.begin(), q.end()) > -zero_tol)
  {
    FILE_LOG(LOG_OPT) << " -- trivial solution found" << endl;
    FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() exited" << endl;
    z.set_zero(n);
    return true;
  }

  // initialize variables
  z.set_zero(n*2);
  unsigned t = 2*n;
  unsigned entering = t;
  unsigned leaving = 0;
  _all.clear();
  for (unsigned i=0; i< n; i++)
    _all.push_back(i);
  unsigned lvindex;
  unsigned idx;
  vector<unsigned>::iterator iiter;
  _tlist.clear();

  // determine initial basis
  _bas.clear();
  _nonbas.clear();
  if (_z0.size() != n)
    for (unsigned i=0; i< n; i++)
      _nonbas.push_back(i);
  else
    for (unsigned i=0; i< n; i++)
      if (_z0[i] > 0)
        _bas.push_back(i);
      else
        _nonbas.push_back(i);

  // determine initial values
  if (!_bas.empty())
  {
    typedef map<pair<unsigned, unsigned>, double> ValueMap;
    ValueMap values, newvalues;

    // select columns of M corresponding to z vars in the basis
    M.get_values(values);
    for (ValueMap::const_iterator i = values.begin(); i != values.end(); i++)
    {
      vector<unsigned>::const_iterator j = std::find(_bas.begin(), _bas.end(), i->first.second);
      if (j == _bas.end())
        continue;
      else
        newvalues[make_pair(i->first.first, j - _bas.begin())] = i->second;
    }

    // "select" columns of eye corresponding to z vars not in the basis
    // select_columns(_nonbas.begin(), _nonbas.end(), _st2);
    for (unsigned i=0, j=_bas.size(); i< _nonbas.size(); i++, j++)
      newvalues[make_pair(_nonbas[i],j)] = 1.0;

    // setup the basis matrix
    _sBl = SparseMatrixNd(SparseMatrixNd::eCSC, n, n, newvalues);
  }
  else
  {
    _sBl = SparseMatrixNd::identity(SparseMatrixNd::eCSC, n);
    _sBl.negate();
  }

  // solve B*x = -q
  _LA.solve_sparse_direct(_sBl, q, Ravelin::eNoTranspose, _x);
  _x.negate();

  // check whether initial basis provides a solution
  if (std::find_if(_x.begin(), _x.end(), std::bind2nd(std::less<double>(), 0.0)) == _x.end())
  {
    for (idx = 0, iiter = _bas.begin(); iiter != _bas.end(); iiter++, idx++)
      z[*iiter] = _x[idx];
    z.resize(n, true);

    // check to see whether tolerances are satisfied
    FILE_LOG(LOG_OPT) << " -- initial basis provides a solution!" << std::endl;
    if (LOGGING(LOG_OPT))
    {
      M.mult(z, _wl) += q;
      double minw = *std::min_element(_wl.begin(), _wl.end());
      double w_dot_z = std::fabs(_wl.dot(z));
      FILE_LOG(LOG_OPT) << "  z: " << z << std::endl;
      FILE_LOG(LOG_OPT) << "  w: " << _wl << std::endl;
      FILE_LOG(LOG_OPT) << "  minimum w: " << minw << std::endl;
      FILE_LOG(LOG_OPT) << "  w'z: " << w_dot_z << std::endl;
    }
    FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() exited" << endl;

    return true; 
  }

  // determine initial leaving variable
  ColumnIteratord min_x = std::min_element(_x.begin(), _x.begin() + n);
  double tval = -*min_x;
  BOOST_FOREACH(unsigned i, _nonbas) // add w variables to basis
    _bas.push_back(i+n);
  lvindex = std::distance(_x.begin(), min_x);
  iiter = _bas.begin();
  std::advance(iiter, lvindex);
  leaving = *iiter;

  // pivot in the artificial variable
  *iiter = t;    // replace w var with _z0 in basic indices
  _u.resize(n);
  for (unsigned i=0; i< n; i++)
    _u[i] = (_x[i] < 0.0) ? 1.0 : 0.0;
  _sBl.mult(_u, _Be);
  _Be.negate();
  _u *= tval;
  _x += _u;
  _x[lvindex] = tval;
  _sBl.set_column(lvindex, _Be);
  FILE_LOG(LOG_OPT) << "  new q: " << _x << endl;

  // main iterations begin here
  for (unsigned iter=0; iter < MAXITER; iter++)
  {
    // check whether done; if not, get new entering variable
    if (leaving == t)
    {
      FILE_LOG(LOG_OPT) << "-- solved LCP successfully!" << endl;
      unsigned idx;
      for (idx = 0, iiter = _bas.begin(); iiter != _bas.end(); iiter++, idx++)
        z[*iiter] = _x[idx];
      z.resize(n, true);

      // verify tolerances
      if (LOGGING(LOG_OPT))
      {
        M.mult(z, _wl) += q;
        double minw = *std::min_element(_wl.begin(), _wl.end());
        double w_dot_z = std::fabs(_wl.dot(z));
        FILE_LOG(LOG_OPT) << "  found solution!" << std::endl;
        FILE_LOG(LOG_OPT) << "  minimum w: " << minw << std::endl;
        FILE_LOG(LOG_OPT) << "  w'z: " << w_dot_z << std::endl;
      }
      FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() exited" << endl;

      return true; 
    }
    else if (leaving < n)
    {
      entering = n + leaving;
      _Be.set_zero(n);
      _Be[leaving] = -1;
    }
    else
    {
      entering = leaving - n;
      M.get_column(entering, _Be);
    }
    _LA.solve_sparse_direct(_sBl, _Be, Ravelin::eNoTranspose, _dl);

    // use a new pivot tolerance if necessary
    const double PIV_TOL = (piv_tol > (double) 0.0) ? piv_tol : std::numeric_limits<double>::epsilon() * n * std::max((double) 1.0, _Be.norm_inf());

    // ** find new leaving variable
    _j.clear();
    for (unsigned i=0; i< _dl.size(); i++)
      if (_dl[i] > PIV_TOL)
        _j.push_back(i);
    // check for no new pivots; ray termination
    if (_j.empty())
    {
      FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() - no new pivots (ray termination)" << endl;
      FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() exited" << endl;

      z.resize(n, true);
      return false;
    }

    FILE_LOG(LOG_OPT) << " -- column of M': " << _dl << endl;

    // select elements j from x and d
    _xj.resize(_j.size());
    _dj.resize(_xj.size());
    select(_x.begin(), _j.begin(), _j.end(), _xj.begin());
    select(_d.begin(), _j.begin(), _j.end(), _dj.begin());

    // compute minimal ratios x(j) + EPS_DOUBLE ./ d(j), d > 0
    _result.resize(_xj.size());
    std::transform(_xj.begin(), _xj.end(), _result.begin(), std::bind2nd(std::plus<double>(), zero_tol));
    std::transform(_result.begin(), _result.end(), _dj.begin(), _result.begin(), std::divides<double>());
    double theta = *std::min_element(_result.begin(), _result.end());

    // NOTE: lexicographic ordering does not appear to be used here to prevent
    // cycling (see [Cottle 1992], pp. 340-342)
    // find indices of minimal ratios, d> 0
    //   divide _x(j) ./ d(j) -- remove elements above the minimum ratio
    std::transform(_xj.begin(), _xj.end(), _dj.begin(), _result.begin(), std::divides<double>());
    for (iiter = _j.begin(), idx = 0; iiter != _j.end(); )
      if (_result[idx++] <= theta)
        iiter++;
      else
        iiter = _j.erase(iiter);

    // if j is empty, then likely the zero tolerance is too low
    if (_j.empty())
    {
      FILE_LOG(LOG_OPT) << "zero tolerance too low?" << std::endl;
      FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() exited" << std::endl;
      z.resize(n, true);
      return false;
    }

    // check whether artificial index among these
    _tlist.clear();
    select(_bas.begin(), _j.begin(), _j.end(), std::back_inserter(_tlist));
    if (std::find(_tlist.begin(), _tlist.end(), t) != _tlist.end())
    {
      iiter = std::find(_bas.begin(), _bas.end(), t); 
      lvindex = iiter - _bas.begin();
    }
    else
    {
      // several indices pass the minimum ratio test, pick one randomly
//      lvindex = _j[rand() % _j.size()];

      // NOTE: solver seems *much* more capable of solving when we pick the first
      // element rather than picking a random one
      lvindex = _j[0];
    }

    // set leaving = bas(lvindex)
    iiter = _bas.begin();
    std::advance(iiter, lvindex);
    leaving = *iiter;

    // ** perform pivot
    double ratio = _x[lvindex]/_dl[lvindex];
    _dl *= ratio;
    _x -= _dl;
    _x[lvindex] = ratio;
    _sBl.set_column(lvindex, _Be);
    *iiter = entering;
    FILE_LOG(LOG_OPT) << " -- pivoting: leaving index=" << lvindex << "  entering index=" << entering << endl;
  }

  FILE_LOG(LOG_OPT) << " -- maximum number of iterations exceeded" << endl;
  FILE_LOG(LOG_OPT) << "LCP::lcp_lemke() exited" << std::endl;

  // max iterations exceeded
  z.resize(n, true);
  
  return false;
}

// picks (randomly) the minimum element from a vector that has potentially multiple minima 
static RowIteratord_const rand_min2(const VectorNd& v)
{
  const double EPS = std::sqrt(std::numeric_limits<double>::epsilon());
  std::vector<unsigned> idx;

  double minimum = *std::min_element(v.row_iterator_begin(), v.row_iterator_end());
  for (RowIteratord_const i = v.row_iterator_begin(); i != v.row_iterator_end(); i++) 
    if (*i - EPS <= minimum)
      idx.push_back(i-v.row_iterator_begin());

  // pick one at random
  assert(!idx.empty());
  unsigned elm = idx[rand() % idx.size()];
  return v.row_iterator_begin()+elm;
}

/// Fast pivoting algorithm for frictionless contact
bool LCP::fast_pivoting(const MatrixNd& M, const VectorNd& q, VectorNd& z, double eps)
{
  const unsigned N = q.size();
  const unsigned MAX_PIVOTS = N*3;
  RowIteratord_const minw, minz;

  // look for degenerate problem
  if (N == 0)
  {
    z.resize(0);
    return true;
  }

  // compute minimum indices
  minw = std::min_element(q.row_iterator_begin(), q.row_iterator_end());

  // look for easy solution
  if (*minw > -eps)
  {
    z.set_zero(N);
    return true;
  }

  // setup the basic variable and non-basic variable indices
  _bas.clear();
  _nonbas.clear();
  for (unsigned i=0; i< N; i++)
    if (i != minw - q.row_iterator_begin())
      _bas.push_back(i);
  _nonbas.push_back(minw-q.row_iterator_begin());

  // start the pivoting algorithm
  for (unsigned i=0; i< MAX_PIVOTS; i++)
  {
    // solve for nonbasic z
    M.select_square(_nonbas.begin(), _nonbas.end(), _M);
    q.select(_nonbas.begin(), _nonbas.end(), _qprime);
    _qprime.negate();

    // compute z subset
    try
    {
      _LA.solve_fast(_M, _qprime);
    }
    catch (SingularException e)
    {
      M.select_square(_nonbas.begin(), _nonbas.end(), _M);
      try
      {
        _LA.solve_LS_fast(_M, _qprime, LinAlgd::eSVD1, -1.0);
      }
      catch (NumericalException e)
      {
        try
        {
          M.select_square(_nonbas.begin(), _nonbas.end(), _M);
          _LA.solve_LS_fast(_M, _qprime, LinAlgd::eSVD2, -1.0);
        }
        catch (NumericalException e)
        {
          return false;
        }
      }
    }    

    // setup proposed z
    z.set_zero(N);
    for (unsigned j=0; j< _nonbas.size(); j++)
      z[_nonbas[j]] = _qprime[j];

    // compute w
    M.mult(z, _w) += q;

    // recompute minimum indices
    minw = rand_min2(_w);
    minz = rand_min2(z);

    // see whether this has solved the problem
    if (*minw > -eps)
    {
      // check whether any component of z < 0
      if (*minz < -eps)
      {
        // move the element to the basic set
        unsigned idx = minz-z.row_iterator_begin();
        _nonbas.erase(std::find(_nonbas.begin(), _nonbas.end(), idx));
        _bas.insert(std::lower_bound(_bas.begin(), _bas.end(), idx), idx); 
      }
      else
        return true;
    }
    else
    {
      // move mimimum component of w to nonbasic set
      unsigned idx = minw-_w.row_iterator_begin();
      _bas.erase(std::find(_bas.begin(), _bas.end(), idx));
      _nonbas.insert(std::lower_bound(_nonbas.begin(), _nonbas.end(), idx), idx);

      // look whether a component of z needs to move to basic set
      if (*minz < -eps)
      {
        // move the element to the basic set
        unsigned idx = minz-z.row_iterator_begin();
        _nonbas.erase(std::find(_nonbas.begin(), _nonbas.end(), idx));
        _bas.insert(std::lower_bound(_bas.begin(), _bas.end(), idx), idx); 
      }
    }
  }

  // if we're here, the maximum number of pivots was exceeded
  std::cerr << "LCP::fast_pivoting() warning- maximum number of pivots exceeded (" << MAX_PIVOTS << ")" << std::endl;
  return false;
}


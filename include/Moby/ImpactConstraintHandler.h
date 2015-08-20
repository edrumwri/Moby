/****************************************************************************
 * Copyright 2011 Evan Drumwright
 * This library is distributed under the terms of the Apache V2.0
 * License (obtainable from http://www.apache.org/licenses/LICENSE-2.0).
 ****************************************************************************/

#ifndef _IMPACT_EVENT_HANDLER_H
#define _IMPACT_EVENT_HANDLER_H

#include <list>
#include <vector>
#include <map>
#ifdef USE_QLCPD
#include <Moby/QLCPD.h>
#endif
#ifdef HAVE_IPOPT
#include <coin/IpTNLP.hpp>
#include <coin/IpIpoptApplication.hpp>
#include <Moby/NQP_IPOPT.h>
#include <Moby/LCP_IPOPT.h>
#endif
#include <Ravelin/LinAlgd.h>
#include <Moby/Base.h>
#include <Moby/Types.h>
#include <Moby/LCP.h>
#include <Moby/SparseJacobian.h>
#include <Moby/UnilateralConstraint.h>
#include <Moby/UnilateralConstraintProblemData.h>

namespace Moby {

class NQP_IPOPT;
class LCP_IPOPT;

/// Defines the mechanism for handling impact constraints
class ImpactConstraintHandler
{
  public:
    ImpactConstraintHandler();
    void process_constraints(const std::vector<UnilateralConstraint>& constraints);

    /// If set to true, uses the interior-point solver (default is false)
    bool use_ip_solver;

    /// The maximum number of iterations to use for the interior-point solver
    unsigned ip_max_iterations;

    /// The tolerance for to the interior-point solver (default 1e-6)
    double ip_eps;

  private:
    void solve_frictionless_lcp(UnilateralConstraintProblemData& q, Ravelin::VectorNd& z);
    void apply_visc_friction_model_to_connected_constraints(const std::list<UnilateralConstraint*>& constraints);
    void apply_no_slip_model_to_connected_constraints(const std::list<UnilateralConstraint*>& constraints);
    void apply_ap_model_to_connected_constraints(const std::list<UnilateralConstraint*>& constraints);
    void update_from_stacked(UnilateralConstraintProblemData& q, const Ravelin::VectorNd& z);
    double calc_min_constraint_velocity(const UnilateralConstraintProblemData& q) const;
    void update_constraint_velocities_from_impulses(UnilateralConstraintProblemData& q);
    bool apply_restitution(const UnilateralConstraintProblemData& q, Ravelin::VectorNd& z) const;
    bool apply_restitution(UnilateralConstraintProblemData& q) const;
    static boost::shared_ptr<Ravelin::DynamicBodyd> get_super_body(boost::shared_ptr<Ravelin::SingleBodyd> sb);
    static bool use_qp_solver(const UnilateralConstraintProblemData& epd);
    void apply_visc_friction_model(UnilateralConstraintProblemData& epd);
    void apply_no_slip_model(UnilateralConstraintProblemData& epd);
    void apply_ap_model(UnilateralConstraintProblemData& epd);
    void solve_qp(Ravelin::VectorNd& z, UnilateralConstraintProblemData& epd);
    void solve_nqp(Ravelin::VectorNd& z, UnilateralConstraintProblemData& epd);
    void apply_model(const std::vector<UnilateralConstraint>& constraints);
    void apply_model_to_connected_constraints(const std::list<UnilateralConstraint*>& constraints);
    void compute_problem_data(UnilateralConstraintProblemData& epd);
    void solve_lcp(UnilateralConstraintProblemData& epd, Ravelin::VectorNd& z);
    void solve_qp_work(UnilateralConstraintProblemData& epd, Ravelin::VectorNd& z);
    double calc_ke(UnilateralConstraintProblemData& epd, const Ravelin::VectorNd& z);
    void update_problem(const UnilateralConstraintProblemData& qorig, UnilateralConstraintProblemData& qnew);
    void update_solution(const UnilateralConstraintProblemData& q, const Ravelin::VectorNd& x, const std::vector<bool>& working_set, unsigned jidx, Ravelin::VectorNd& z);
    void solve_nqp_work(UnilateralConstraintProblemData& epd, Ravelin::VectorNd& z);
    void propagate_impulse_data(const UnilateralConstraintProblemData& epd);
    void apply_impulses(const UnilateralConstraintProblemData& epd);
    static void contact_select(const std::vector<int>& cn_indices, const std::vector<int>& beta_nbeta_c_indices, const Ravelin::VectorNd& x, Ravelin::VectorNd& cn, Ravelin::VectorNd& beta_c);
    static void contact_select(const std::vector<int>& cn_indices, const std::vector<int>& beta_nbeta_c_indices, const Ravelin::MatrixNd& m, Ravelin::MatrixNd& cn_rows, Ravelin::MatrixNd& beta_c_rows);
    static double sqr(double x) { return x*x; }
    void setup_QP(UnilateralConstraintProblemData& epd, Ravelin::SharedMatrixNd& H, Ravelin::SharedVectorNd& c, Ravelin::SharedMatrixNd& M, Ravelin::SharedVectorNd& q, Ravelin::SharedMatrixNd& A, Ravelin::SharedVectorNd& b);
    static void get_full_rank_implicit_constraints(const SparseJacobian& J, std::vector<bool>& active);
    static Ravelin::MatrixNd& mult(const std::vector<Ravelin::MatrixNd>& inertias, const Ravelin::MatrixNd& X, Ravelin::MatrixNd& B);
    static Ravelin::MatrixNd& to_dense(const std::vector<Ravelin::MatrixNd>& J, Ravelin::MatrixNd& B);

    Ravelin::LinAlgd _LA;
    LCP _lcp;

    // persistent constraint data
    UnilateralConstraintProblemData _epd;

    // temporaries for compute_problem_data(), solve_qp_work(), solve_lcp(), and apply_impulses()
    Ravelin::MatrixNd _MM;
    Ravelin::VectorNd _zlast, _v;

    // temporaries for solve_qp_work() and solve_nqp_work()
    Ravelin::VectorNd _workv, _new_Cn_v;

    // temporaries shared between solve_lcp(), solve_qp(), and solve_nqp()
    Ravelin::VectorNd _a, _b;

    // temporaries for solve_qp() and solve_nqp()
    Ravelin::VectorNd _z;

    // temporaries for solve_frictionless_lcp()
    Ravelin::VectorNd _cs_visc, _ct_visc;

    // temporaries for solve_no_slip_lcp()
    Ravelin::MatrixNd _rJx_iM_JxT, _Y, _Q_iM_XT, _workM, _workM2;
    Ravelin::VectorNd _YXv, _Xv, _cs_ct_alphax;

    // interior-point solver "application"
    #ifdef HAVE_IPOPT
    Ipopt::IpoptApplication _app;
    #endif

    // temporaries shared between solve_nqp_work() and solve_lcp()
    Ravelin::MatrixNd _A;

    // temporaries for solve_nqp_work()
    #ifdef HAVE_IPOPT
    Ipopt::SmartPtr <NQP_IPOPT> _ipsolver;
    Ipopt::SmartPtr <LCP_IPOPT> _lcpsolver;
    #endif
    Ravelin::MatrixNd _RTH;
    Ravelin::VectorNd _w, _workv2, _x;

    // temporaries for solve_lcp()
    Ravelin::MatrixNd _AU, _AV, _B, _C, _D;
    Ravelin::VectorNd _AS, _alpha_x, _qq, _Cn_vplus;

    // QLCPD solver
    #ifdef USE_QLCPD
    QLCPD _qp;
    #endif

/*
    // temporaries for IPOPT
    boost::shared_ptr<double> _h_obj, _cJac;
    std::vector<boost::shared_ptr<double> > _h_con;
    unsigned _nnz_h_obj;
    std::vector<unsigned> _nnz_h_con;
    boost::shared_ptr<unsigned> _h_iRow, _h_jCol, _cJac_iRow, _cJac_jCol;
*/
}; // end class
} // end namespace

#endif


/*******************************************************************************
 *
 * Buffer overflow checker.
 *
 * Author: Maxime Arthaud
 *
 * Contact: ikos@lists.nasa.gov
 *
 * Notices:
 *
 * Copyright (c) 2011-2016 United States Government as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 * All Rights Reserved.
 *
 * Disclaimers:
 *
 * No Warranty: THE SUBJECT SOFTWARE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY OF
 * ANY KIND, EITHER EXPRESSED, IMPLIED, OR STATUTORY, INCLUDING, BUT NOT LIMITED
 * TO, ANY WARRANTY THAT THE SUBJECT SOFTWARE WILL CONFORM TO SPECIFICATIONS,
 * ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE,
 * OR FREEDOM FROM INFRINGEMENT, ANY WARRANTY THAT THE SUBJECT SOFTWARE WILL BE
 * ERROR FREE, OR ANY WARRANTY THAT DOCUMENTATION, IF PROVIDED, WILL CONFORM TO
 * THE SUBJECT SOFTWARE. THIS AGREEMENT DOES NOT, IN ANY MANNER, CONSTITUTE AN
 * ENDORSEMENT BY GOVERNMENT AGENCY OR ANY PRIOR RECIPIENT OF ANY RESULTS,
 * RESULTING DESIGNS, HARDWARE, SOFTWARE PRODUCTS OR ANY OTHER APPLICATIONS
 * RESULTING FROM USE OF THE SUBJECT SOFTWARE.  FURTHER, GOVERNMENT AGENCY
 * DISCLAIMS ALL WARRANTIES AND LIABILITIES REGARDING THIRD-PARTY SOFTWARE,
 * IF PRESENT IN THE ORIGINAL SOFTWARE, AND DISTRIBUTES IT "AS IS."
 *
 * Waiver and Indemnity:  RECIPIENT AGREES TO WAIVE ANY AND ALL CLAIMS AGAINST
 * THE UNITED STATES GOVERNMENT, ITS CONTRACTORS AND SUBCONTRACTORS, AS WELL
 * AS ANY PRIOR RECIPIENT.  IF RECIPIENT'S USE OF THE SUBJECT SOFTWARE RESULTS
 * IN ANY LIABILITIES, DEMANDS, DAMAGES, EXPENSES OR LOSSES ARISING FROM SUCH
 * USE, INCLUDING ANY DAMAGES FROM PRODUCTS BASED ON, OR RESULTING FROM,
 * RECIPIENT'S USE OF THE SUBJECT SOFTWARE, RECIPIENT SHALL INDEMNIFY AND HOLD
 * HARMLESS THE UNITED STATES GOVERNMENT, ITS CONTRACTORS AND SUBCONTRACTORS,
 * AS WELL AS ANY PRIOR RECIPIENT, TO THE EXTENT PERMITTED BY LAW.
 * RECIPIENT'S SOLE REMEDY FOR ANY SUCH MATTER SHALL BE THE IMMEDIATE,
 * UNILATERAL TERMINATION OF THIS AGREEMENT.
 *
 ******************************************************************************/

#ifndef ANALYZER_BUFFER_OVERFLOW_CHECKER_HPP
#define ANALYZER_BUFFER_OVERFLOW_CHECKER_HPP

#include <analyzer/analysis/common.hpp>
#include <analyzer/checkers/checker_api.hpp>
#include <analyzer/analysis/num_sym_exec.hpp>

namespace analyzer {

using namespace arbos;

template < class AbsDomain >
class buffer_overflow_checker : public checker< AbsDomain > {
private:
  typedef checker< AbsDomain > checker_t;
  typedef typename analysis_db::db_ptr db_ptr_t;

  typedef typename AbsDomain::variable_t variable_t;
  typedef typename AbsDomain::linear_expression_t linear_expression_t;

public:
  buffer_overflow_checker(context& ctx, db_ptr_t db) : checker_t(ctx, db) {}

  virtual const char* name() { return "boa"; }
  virtual const char* description() { return "Buffer overflow checker"; }

  virtual void check(Store_ref store,
                     AbsDomain inv,
                     const std::string& call_context) {
    Operand_ref store_pointer = ar::getPointer(store);

    // Skip if writing to an integer/floating point global
    // variable or a local variable whose address has been taken
    // since it's always safe
    if ((!ar::isGlobalVar(store_pointer)) &&
        (!ar::isAllocaVar(store_pointer))) {
      location loc = ar::getSrcLoc(store);
#ifdef DISPLAY_CHECKS
      std::cout << call_context << "|" << loc.first << "|" << loc.second
                << std::endl;
#endif
      check_overflow(store_pointer, inv, call_context, loc);
      check_underflow(store_pointer, inv, call_context, loc);
    }
  }

  virtual void check(Load_ref load,
                     AbsDomain inv,
                     const std::string& call_context) {
    Operand_ref load_pointer = ar::getPointer(load);

    // Skip if reading to an integer/floating point global
    // variable or a local variable whose address has been taken
    // since it's always safe
    if ((!ar::isGlobalVar(load_pointer)) && (!ar::isAllocaVar(load_pointer))) {
      location loc = ar::getSrcLoc(load);
#ifdef DISPLAY_CHECKS
      std::cout << call_context << "|" << loc.first << "|" << loc.second
                << std::endl;
#endif
      check_overflow(load_pointer, inv, call_context, loc);
      check_underflow(load_pointer, inv, call_context, loc);
    }
  }

  virtual void check(MemCpy_ref memcpy,
                     AbsDomain inv,
                     const std::string& call_context) {
    LiteralFactory& lfac = this->_context.lit_factory();
    Literal dest = lfac[ar::getTarget(memcpy)];
    Literal src = lfac[ar::getSource(memcpy)];
    Literal len = lfac[ar::getLen(memcpy)];

    // Both dest and src are already allocated in memory so we need to
    // check that their offsets are in-bounds.
    check_mem_intr_ptr(src, len, inv, call_context, ar::getSrcLoc(memcpy));
    check_mem_intr_ptr(dest, len, inv, call_context, ar::getSrcLoc(memcpy));
  }

  virtual void check(MemMove_ref memmove,
                     AbsDomain inv,
                     const std::string& call_context) {
    LiteralFactory& lfac = this->_context.lit_factory();
    Literal dest = lfac[ar::getTarget(memmove)];
    Literal src = lfac[ar::getSource(memmove)];
    Literal len = lfac[ar::getLen(memmove)];

    // Both dest and src are already allocated in memory so we need to
    // check that their offsets are in-bounds.
    check_mem_intr_ptr(src, len, inv, call_context, ar::getSrcLoc(memmove));
    check_mem_intr_ptr(dest, len, inv, call_context, ar::getSrcLoc(memmove));
  }

  virtual void check(MemSet_ref memset,
                     AbsDomain inv,
                     const std::string& call_context) {
    LiteralFactory& lfac = this->_context.lit_factory();
    Literal dest = lfac[ar::getBase(memset)];
    Literal len = lfac[ar::getLen(memset)];

    // dest is already allocated in memory so we need to check
    // that its offset is in-bounds.
    check_mem_intr_ptr(dest, len, inv, call_context, ar::getSrcLoc(memset));
  }

private:
  void check_mem_intr_ptr(const Literal& ptr,
                          const Literal& len,
                          AbsDomain inv,
                          const std::string& call_context,
                          location loc) {
    if (!(ptr.is_var() && (len.is_var() || len.is_num()))) {
      // this can happen if for instance ptr and len can be undefined
      this->_db->write("overflow",
                       call_context,
                       loc.first,
                       loc.second,
                       "warning");
      return;
    }

    varname_t ptr_var = ptr.get_var();
    linear_expression_t lenght_expr =
        len.is_var() ? linear_expression_t(len.get_var())
                     : linear_expression_t(len.get_num< ikos::z_number >());
    analysis_result over_result =
        check_overflow(ptr_var, variable_t(ptr_var) + lenght_expr - 1, inv);
    analysis_result under_result =
        check_underflow(ptr_var, variable_t(ptr_var), inv);

#ifdef DISPLAY_CHECKS
    std::cout << call_context << "|" << loc.first << "|" << loc.second
              << std::endl;
#endif

#ifdef DISPLAY_INVARIANTS
    if (over_result == ERR || over_result == WARNING || under_result == ERR ||
        under_result == WARNING) {
      std::cout << "Invariant at " << location_to_string(loc) << std::endl
                << inv << std::endl;
    }
#endif

    this->_db->write("overflow",
                     call_context,
                     loc.first,
                     loc.second,
                     tostr(over_result));
    this->_db->write("underflow",
                     call_context,
                     loc.first,
                     loc.second,
                     tostr(under_result));
  }

  void check_overflow(const Operand_ref& ptr,
                      AbsDomain inv,
                      const std::string& call_context,
                      location loc) {
    LiteralFactory& lfac = this->_context.lit_factory();
    Literal ptr_lit = lfac[ptr];
    assert(ptr_lit.is_var());
    varname_t ptr_var = ptr_lit.get_var();
    analysis_result result = check_overflow(ptr_var, variable_t(ptr_var), inv);

#ifdef DISPLAY_INVARIANTS
    if (result == ERR || result == WARNING) {
      std::cout << "Invariant at " << location_to_string(loc) << std::endl
                << inv << std::endl;
    }
#endif

    this->_db->write("overflow",
                     call_context,
                     loc.first,
                     loc.second,
                     tostr(result));
  }

  void check_underflow(const Operand_ref& ptr,
                       AbsDomain inv,
                       const std::string& call_context,
                       location loc) {
    LiteralFactory& lfac = this->_context.lit_factory();
    Literal ptr_lit = lfac[ptr];
    assert(ptr_lit.is_var());
    varname_t ptr_var = ptr_lit.get_var();
    analysis_result result = check_underflow(ptr_var, variable_t(ptr_var), inv);

#ifdef DISPLAY_INVARIANTS
    if (result == ERR || result == WARNING)
      std::cout << "Invariant at " << location_to_string(loc) << std::endl
                << inv << std::endl;
#endif

    this->_db->write("underflow",
                     call_context,
                     loc.first,
                     loc.second,
                     tostr(result));
  }

  analysis_result check_overflow(varname_t pointer,
                                 linear_expression_t offset,
                                 AbsDomain inv) {
    if (inv.is_bottom()) {
      return UNREACHABLE;
    }

    if (value_domain_impl::is_unknown_addr(inv, pointer)) {
      return WARNING;
    } else {
      std::vector< varname_t > addrs_set =
          value_domain_impl::get_addrs_set(inv, pointer);
      assert(!addrs_set.empty());
      bool all_valid = true;
      bool all_invalid = true;

      for (typename std::vector< varname_t >::iterator it = addrs_set.begin();
           it != addrs_set.end();
           ++it) {
        varname_t size_var = num_sym_exec_impl::get_shadow_size(*it);

        if (all_valid) {
          AbsDomain tmp(inv);
          tmp += (offset >= variable_t(size_var));
          all_valid = all_valid && tmp.is_bottom();
        }

        if (all_invalid) {
          AbsDomain tmp(inv);
          tmp += (offset >= 0);
          tmp += (offset <= variable_t(size_var) - 1);
          all_invalid = all_invalid && tmp.is_bottom();
        }
      }

      if (all_invalid) {
        return ERR;
      } else if (!all_valid) {
        return WARNING;
      } else {
        return OK;
      }
    }
  }

  analysis_result check_underflow(varname_t pointer,
                                  linear_expression_t offset,
                                  AbsDomain inv) {
    if (inv.is_bottom()) {
      return UNREACHABLE;
    }

    if (value_domain_impl::is_unknown_addr(inv, pointer)) {
      return WARNING;
    } else {
      AbsDomain tmp(inv);
      tmp += (offset <= -1);
      if (!tmp.is_bottom()) { // warning or error
        inv += (offset >= 0);
        if (inv.is_bottom()) {
          return ERR;
        } else {
          return WARNING;
        }
      }

      return OK;
    }
  }
}; // end class buffer_overflow_checker

} // end namespace analyzer

#endif // ANALYZER_BUFFER_OVERFLOW_CHECKER_HPP

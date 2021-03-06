/* Copyright (c) 2015 The Khronos Group Inc.

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and/or associated documentation files (the
   "Materials"), to deal in the Materials without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Materials, and to
   permit persons to whom the Materials are furnished to do so, subject to
   the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Materials.

   MODIFICATIONS TO THIS FILE MAY MEAN IT NO LONGER ACCURATELY REFLECTS
   KHRONOS STANDARDS. THE UNMODIFIED, NORMATIVE VERSIONS OF KHRONOS
   SPECIFICATIONS AND HEADER INFORMATION ARE LOCATED AT
    https://www.khronos.org/registry/

  THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.

*/

#ifndef __SYCL_IMPL_ALGORITHM_TRANSFORM_REDUCE__
#define __SYCL_IMPL_ALGORITHM_TRANSFORM_REDUCE__

#include <type_traits>
#include <algorithm>
#include <iostream>

// SYCL helpers header
#include <sycl/helpers/sycl_buffers.hpp>
#include <sycl/helpers/sycl_differences.hpp>
#include <sycl/algorithm/algorithm_composite_patterns.hpp>

namespace sycl {
namespace impl {

/* transform_reduce.
* @brief Returns the transform_reduce of one vector across the range [first1,
* last1) by applying Functions op1 and op2. Implementation of the command
* group
* that submits a transform_reduce kernel.
*/
template <class ExecutionPolicy, class InputIterator, class UnaryOperation,
          class T, class BinaryOperation>
T transform_reduce(ExecutionPolicy& exec, InputIterator first,
                   InputIterator last, UnaryOperation unary_op, T init,
                   BinaryOperation binary_op) {
  cl::sycl::queue q(exec.get_queue());
  auto vectorSize = sycl::helpers::distance(first, last);
  cl::sycl::buffer<T, 1> bufR((cl::sycl::range<1>(vectorSize)));
  if (vectorSize < 1) {
    return init;
  }

  auto device = q.get_device();
  auto local =
      std::min(device.get_info<cl::sycl::info::device::max_work_group_size>(),
               vectorSize);
  typedef typename std::iterator_traits<InputIterator>::value_type type_;
  auto bufI = sycl::helpers::make_const_buffer(first, last);
  size_t length = vectorSize;
  size_t global = exec.calculateGlobalSize(vectorSize, local);
  int passes = 0;

  do {
    auto f = [passes, length, local, global, &bufI, &bufR, unary_op, binary_op](
        cl::sycl::handler& h) mutable {
      cl::sycl::nd_range<3> r{cl::sycl::range<3>{std::max(global, local), 1, 1},
                              cl::sycl::range<3>{local, 1, 1}};
      auto aI = bufI.template get_access<cl::sycl::access::mode::read>(h);
      auto aR = bufR.template get_access<cl::sycl::access::mode::read_write>(h);
      cl::sycl::accessor<T, 1, cl::sycl::access::mode::read_write,
                         cl::sycl::access::target::local>
          scratch(cl::sycl::range<1>(local), h);

      h.parallel_for<typename ExecutionPolicy::kernelName>(
          r, [aI, aR, scratch, passes, local, length, unary_op, binary_op](
                 cl::sycl::nd_item<3> id) {
            int globalid = id.get_global(0);
            int localid = id.get_local(0);
            auto r = ReductionStrategy<T>(local, length, id, scratch);
            if (passes == 0) {
              r.workitem_get_from(unary_op, aI);
            } else {
              r.workitem_get_from(aR);
            }
            r.combine_threads(binary_op);
            r.workgroup_write_to(aR);
          });
    };
    q.submit(f);
    passes++;
    length = length / local;
  } while (length > 1);
  q.wait_and_throw();
  auto hR = bufR.template get_access<cl::sycl::access::mode::read,
                                     cl::sycl::access::target::host_buffer>();
  return binary_op(hR[0], init);
}

}  // namespace impl
}  // namespace sycl

#endif  // __SYCL_IMPL_ALGORITHM_TRANSFORM_REDUCE__

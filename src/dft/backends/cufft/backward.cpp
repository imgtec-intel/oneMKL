/*******************************************************************************
* Copyright Codeplay Software Ltd.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing,
* software distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions
* and limitations under the License.
*
*
* SPDX-License-Identifier: Apache-2.0
*******************************************************************************/

#if __has_include(<sycl/sycl.hpp>)
#include <sycl/sycl.hpp>
#else
#include <CL/sycl.hpp>
#endif

#include "oneapi/mkl/exceptions.hpp"

#include "oneapi/mkl/dft/detail/commit_impl.hpp"
#include "oneapi/mkl/dft/detail/cufft/onemkl_dft_cufft.hpp"
#include "oneapi/mkl/dft/types.hpp"

#include "execute_helper.hpp"

#include <cufft.h>

namespace oneapi::mkl::dft::cufft {
namespace detail {
template <dft::precision prec, dft::domain dom>
cufftHandle get_bwd_plan(dft::detail::commit_impl<prec, dom> *commit) {
    return static_cast<std::optional<cufftHandle> *>(commit->get_handle())[1].value();
}
} // namespace detail
// BUFFER version

//In-place transform
template <typename descriptor_type>
ONEMKL_EXPORT void compute_backward(descriptor_type &desc,
                                    sycl::buffer<fwd<descriptor_type>, 1> &inout) {
    detail::expect_config<dft::config_param::PLACEMENT, dft::config_value::INPLACE>(
        desc, "Unexpected value for placement");
    auto commit = detail::checked_get_commit(desc);
    auto queue = commit->get_queue();
    auto plan = detail::get_bwd_plan(commit);

    queue.submit([&](sycl::handler &cgh) {
        auto inout_acc = inout.template get_access<sycl::access::mode::read_write>(cgh);

        cgh.host_task([=](sycl::interop_handle ih) {
            const std::string func_name = "compute_backward(desc, inout)";
            auto stream = detail::setup_stream(func_name, ih, plan);

            auto inout_native = reinterpret_cast<void *>(
                ih.get_native_mem<sycl::backend::ext_oneapi_cuda>(inout_acc));
            detail::cufft_execute<detail::Direction::Backward, fwd<descriptor_type>>(
                func_name, stream, plan, inout_native, inout_native);
        });
    });
}

//In-place transform, using config_param::COMPLEX_STORAGE=config_value::REAL_REAL data format
template <typename descriptor_type>
ONEMKL_EXPORT void compute_backward(descriptor_type &, sycl::buffer<scalar<descriptor_type>, 1> &,
                                    sycl::buffer<scalar<descriptor_type>, 1> &) {
    throw oneapi::mkl::unimplemented("DFT", "compute_backward(desc, inout_re, inout_im)",
                                     "cuFFT does not support real-real complex storage.");
}

//Out-of-place transform
template <typename descriptor_type>
ONEMKL_EXPORT void compute_backward(descriptor_type &desc,
                                    sycl::buffer<bwd<descriptor_type>, 1> &in,
                                    sycl::buffer<fwd<descriptor_type>, 1> &out) {
    detail::expect_config<dft::config_param::PLACEMENT, dft::config_value::NOT_INPLACE>(
        desc, "Unexpected value for placement");
    auto commit = detail::checked_get_commit(desc);
    auto queue = commit->get_queue();
    auto plan = detail::get_bwd_plan(commit);

    queue.submit([&](sycl::handler &cgh) {
        auto in_acc = in.template get_access<sycl::access::mode::read_write>(cgh);
        auto out_acc = out.template get_access<sycl::access::mode::read_write>(cgh);

        cgh.host_task([=](sycl::interop_handle ih) {
            const std::string func_name = "compute_backward(desc, in, out)";
            auto stream = detail::setup_stream(func_name, ih, plan);

            auto in_native =
                reinterpret_cast<void *>(ih.get_native_mem<sycl::backend::ext_oneapi_cuda>(in_acc));
            auto out_native = reinterpret_cast<void *>(
                ih.get_native_mem<sycl::backend::ext_oneapi_cuda>(out_acc));
            detail::cufft_execute<detail::Direction::Backward, fwd<descriptor_type>>(
                func_name, stream, plan, in_native, out_native);
        });
    });
}

//Out-of-place transform, using config_param::COMPLEX_STORAGE=config_value::REAL_REAL data format
template <typename descriptor_type>
ONEMKL_EXPORT void compute_backward(descriptor_type &, sycl::buffer<scalar<descriptor_type>, 1> &,
                                    sycl::buffer<scalar<descriptor_type>, 1> &,
                                    sycl::buffer<scalar<descriptor_type>, 1> &,
                                    sycl::buffer<scalar<descriptor_type>, 1> &) {
    throw oneapi::mkl::unimplemented("DFT", "compute_backward(desc, in_re, in_im, out_re, out_im)",
                                     "cuFFT does not support real-real complex storage.");
}

//USM version

//In-place transform
template <typename descriptor_type>
ONEMKL_EXPORT sycl::event compute_backward(descriptor_type &desc, fwd<descriptor_type> *inout,
                                           const std::vector<sycl::event> &dependencies) {
    detail::expect_config<dft::config_param::PLACEMENT, dft::config_value::INPLACE>(
        desc, "Unexpected value for placement");
    auto commit = detail::checked_get_commit(desc);
    auto queue = commit->get_queue();
    auto plan = detail::get_bwd_plan(commit);

    return queue.submit([&](sycl::handler &cgh) {
        cgh.depends_on(dependencies);

        cgh.host_task([=](sycl::interop_handle ih) {
            const std::string func_name = "compute_backward(desc, inout, dependencies)";
            auto stream = detail::setup_stream(func_name, ih, plan);

            detail::cufft_execute<detail::Direction::Backward, fwd<descriptor_type>>(
                func_name, stream, plan, inout, inout);
        });
    });
}

//In-place transform, using config_param::COMPLEX_STORAGE=config_value::REAL_REAL data format
template <typename descriptor_type>
ONEMKL_EXPORT sycl::event compute_backward(descriptor_type &, scalar<descriptor_type> *,
                                           scalar<descriptor_type> *,
                                           const std::vector<sycl::event> &) {
    throw oneapi::mkl::unimplemented("DFT",
                                     "compute_backward(desc, inout_re, inout_im, dependencies)",
                                     "cuFFT does not support real-real complex storage.");
}

//Out-of-place transform
template <typename descriptor_type>
ONEMKL_EXPORT sycl::event compute_backward(descriptor_type &desc, bwd<descriptor_type> *in,
                                           fwd<descriptor_type> *out,
                                           const std::vector<sycl::event> &dependencies) {
    detail::expect_config<dft::config_param::PLACEMENT, dft::config_value::NOT_INPLACE>(
        desc, "Unexpected value for placement");
    auto commit = detail::checked_get_commit(desc);
    auto queue = commit->get_queue();
    auto plan = detail::get_bwd_plan(commit);

    return queue.submit([&](sycl::handler &cgh) {
        cgh.depends_on(dependencies);

        cgh.host_task([=](sycl::interop_handle ih) {
            const std::string func_name = "compute_backward(desc, in, out, dependencies)";
            auto stream = detail::setup_stream(func_name, ih, plan);

            detail::cufft_execute<detail::Direction::Backward, fwd<descriptor_type>>(
                func_name, stream, plan, in, out);
        });
    });
}

//Out-of-place transform, using config_param::COMPLEX_STORAGE=config_value::REAL_REAL data format
template <typename descriptor_type>
ONEMKL_EXPORT sycl::event compute_backward(descriptor_type &, scalar<descriptor_type> *,
                                           scalar<descriptor_type> *, scalar<descriptor_type> *,
                                           scalar<descriptor_type> *,
                                           const std::vector<sycl::event> &) {
    throw oneapi::mkl::unimplemented("DFT",
                                     "compute_backward(desc, in_re, in_im, out_re, out_im, deps)",
                                     "cuFFT does not support real-real complex storage.");
}

// Template function instantiations
#include "dft/backends/backend_backward_instantiations.cxx"

} // namespace oneapi::mkl::dft::cufft

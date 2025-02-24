/*******************************************************************************
* Copyright 2023 Intel Corporation
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

#include "oneapi/mkl/dft/descriptor.hpp"
#include "oneapi/mkl/dft/detail/mklcpu/onemkl_dft_mklcpu.hpp"
#include "oneapi/mkl/dft/types.hpp"

#include "dft/backends/mklcpu/commit_derived_impl.hpp"

// MKLCPU header
#include "mkl_dfti.h"

namespace oneapi::mkl::dft::mklcpu {
namespace detail {

// BUFFER version
// backward a MKLCPU DFT call to the backend, checking that the commit impl is valid.
template <dft::precision prec, dft::domain dom>
inline void check_bwd_commit(dft::descriptor<prec, dom> &desc) {
    auto commit_handle = dft::detail::get_commit(desc);
    if (commit_handle == nullptr || commit_handle->get_backend() != backend::mklcpu) {
        throw mkl::invalid_argument("DFT", "computer_backward",
                                    "DFT descriptor has not been commited for MKLCPU");
    }

    auto mklcpu_desc = reinterpret_cast<detail::mklcpu_desc_t *>(commit_handle->get_handle());
    MKL_LONG commit_status{ DFTI_UNCOMMITTED };
    DftiGetValue(mklcpu_desc[1], DFTI_COMMIT_STATUS, &commit_status);
    if (commit_status != DFTI_COMMITTED) {
        throw mkl::invalid_argument("DFT", "compute_backward",
                                    "MKLCPU DFT descriptor was not successfully committed.");
    }
}

// Throw an mkl::invalid_argument if the runtime param in the descriptor does not match
// the expected value.
template <dft::detail::config_param Param, dft::detail::config_value Expected, typename DescT>
inline auto expect_config(DescT &desc, const char *message) {
    dft::detail::config_value actual{ 0 };
    desc.get_value(Param, &actual);
    if (actual != Expected) {
        throw mkl::invalid_argument("DFT", "compute_backward", message);
    }
}
// convert the base commit class to derived cpu commit class
template <dft::precision prec, dft::domain dom>
auto get_buffer(commit_t<prec, dom> *commit_handle) {
    commit_derived_t<prec, dom> *derived_commit =
        static_cast<commit_derived_t<prec, dom> *>(commit_handle);
    return derived_commit->get_handle_buffer();
}
} // namespace detail

//In-place transform
template <typename descriptor_type>
ONEMKL_EXPORT void compute_backward(descriptor_type &desc,
                                    sycl::buffer<fwd<descriptor_type>, 1> &inout) {
    detail::expect_config<dft::detail::config_param::PLACEMENT, dft::detail::config_value::INPLACE>(
        desc, "Unexpected value for placement");
    auto commit_handle = dft::detail::get_commit(desc);
    detail::check_bwd_commit(desc);
    sycl::queue &cpu_queue{ commit_handle->get_queue() };

    auto mklcpu_desc_buffer{ detail::get_buffer(commit_handle) };

    cpu_queue.submit([&](sycl::handler &cgh) {
        auto desc_acc = mklcpu_desc_buffer.template get_access<sycl::access::mode::read>(cgh);
        auto inout_acc = inout.template get_access<sycl::access::mode::read_write>(cgh);
        detail::host_task<class host_kernel_back_inplace>(cgh, [=]() {
            DFT_ERROR status =
                DftiComputeBackward(desc_acc[detail::DIR::bwd], inout_acc.get_pointer());
            if (status != DFTI_NO_ERROR) {
                throw oneapi::mkl::exception(
                    "dft/backends/mklcpu", "compute_backward",
                    std::string("DftiComputeBackward failed : ") + DftiErrorMessage(status));
            }
        });
    });
}

//In-place transform, using config_param::COMPLEX_STORAGE=config_value::REAL_REAL data format
template <typename descriptor_type>
ONEMKL_EXPORT void compute_backward(descriptor_type &desc,
                                    sycl::buffer<scalar<descriptor_type>, 1> &inout_re,
                                    sycl::buffer<scalar<descriptor_type>, 1> &inout_im) {
    detail::expect_config<dft::detail::config_param::COMPLEX_STORAGE,
                          dft::detail::config_value::REAL_REAL>(
        desc, "Unexpected value for complex storage");

    auto commit_handle = dft::detail::get_commit(desc);
    detail::check_bwd_commit(desc);
    sycl::queue &cpu_queue{ commit_handle->get_queue() };

    auto mklcpu_desc_buffer{ detail::get_buffer(commit_handle) };

    cpu_queue.submit([&](sycl::handler &cgh) {
        auto desc_acc = mklcpu_desc_buffer.template get_access<sycl::access::mode::read>(cgh);
        auto re_acc = inout_re.template get_access<sycl::access::mode::read_write>(cgh);
        auto im_acc = inout_im.template get_access<sycl::access::mode::read_write>(cgh);

        detail::host_task<class host_kernel_split_back_inplace>(cgh, [=]() {
            DFT_ERROR status = DftiComputeBackward(desc_acc[detail::DIR::bwd], re_acc.get_pointer(),
                                                   im_acc.get_pointer());
            if (status != DFTI_NO_ERROR) {
                throw oneapi::mkl::exception(
                    "dft/backends/mklcpu", "compute_backward",
                    std::string("DftiComputeBackward failed : ") + DftiErrorMessage(status));
            }
        });
    });
}

//Out-of-place transform
template <typename descriptor_type>
ONEMKL_EXPORT void compute_backward(descriptor_type &desc,
                                    sycl::buffer<bwd<descriptor_type>, 1> &in,
                                    sycl::buffer<fwd<descriptor_type>, 1> &out) {
    detail::expect_config<dft::detail::config_param::PLACEMENT,
                          dft::detail::config_value::NOT_INPLACE>(desc,
                                                                  "Unexpected value for placement");

    auto commit_handle = dft::detail::get_commit(desc);
    detail::check_bwd_commit(desc);
    sycl::queue &cpu_queue{ commit_handle->get_queue() };

    auto mklcpu_desc_buffer{ detail::get_buffer(commit_handle) };

    cpu_queue.submit([&](sycl::handler &cgh) {
        auto desc_acc = mklcpu_desc_buffer.template get_access<sycl::access::mode::read>(cgh);
        auto in_acc = in.template get_access<sycl::access::mode::read>(cgh);
        auto out_acc = out.template get_access<sycl::access::mode::write>(cgh);

        detail::host_task<class host_kernel_back_outofplace>(cgh, [=]() {
            auto in_ptr = const_cast<bwd<descriptor_type> *>(&in_acc.get_pointer()[0]);
            DFT_ERROR status =
                DftiComputeBackward(desc_acc[detail::DIR::bwd], in_ptr, out_acc.get_pointer());
            if (status != DFTI_NO_ERROR) {
                throw oneapi::mkl::exception(
                    "dft/backends/mklcpu", "compute_backward",
                    std::string("DftiComputeBackward failed : ") + DftiErrorMessage(status));
            }
        });
    });
}

//Out-of-place transform, using config_param::COMPLEX_STORAGE=config_value::REAL_REAL data format
template <typename descriptor_type>
ONEMKL_EXPORT void compute_backward(descriptor_type &desc,
                                    sycl::buffer<scalar<descriptor_type>, 1> &in_re,
                                    sycl::buffer<scalar<descriptor_type>, 1> &in_im,
                                    sycl::buffer<scalar<descriptor_type>, 1> &out_re,
                                    sycl::buffer<scalar<descriptor_type>, 1> &out_im) {
    detail::expect_config<dft::detail::config_param::COMPLEX_STORAGE,
                          dft::detail::config_value::REAL_REAL>(
        desc, "Unexpected value for complex storage");

    auto commit_handle = dft::detail::get_commit(desc);
    detail::check_bwd_commit(desc);
    sycl::queue &cpu_queue{ commit_handle->get_queue() };

    auto mklcpu_desc_buffer{ detail::get_buffer(commit_handle) };

    cpu_queue.submit([&](sycl::handler &cgh) {
        auto desc_acc = mklcpu_desc_buffer.template get_access<sycl::access::mode::read>(cgh);
        auto inre_acc = in_re.template get_access<sycl::access::mode::read>(cgh);
        auto inim_acc = in_im.template get_access<sycl::access::mode::read>(cgh);
        auto outre_acc = out_re.template get_access<sycl::access::mode::write>(cgh);
        auto outim_acc = out_im.template get_access<sycl::access::mode::write>(cgh);

        detail::host_task<class host_kernel_split_back_outofplace>(cgh, [=]() {
            auto inre_ptr = const_cast<scalar<descriptor_type> *>(&inre_acc.get_pointer()[0]);
            auto inim_ptr = const_cast<scalar<descriptor_type> *>(&inim_acc.get_pointer()[0]);
            DFT_ERROR status =
                DftiComputeBackward(desc_acc[detail::DIR::bwd], inre_ptr, inim_ptr,
                                    outre_acc.get_pointer(), outim_acc.get_pointer());
            if (status != DFTI_NO_ERROR) {
                throw oneapi::mkl::exception(
                    "dft/backends/mklcpu", "compute_backward",
                    std::string("DftiComputeBackward failed : ") + DftiErrorMessage(status));
            }
        });
    });
}

//USM version

//In-place transform
template <typename descriptor_type>
ONEMKL_EXPORT sycl::event compute_backward(descriptor_type &desc, fwd<descriptor_type> *inout,
                                           const std::vector<sycl::event> &dependencies) {
    detail::expect_config<dft::detail::config_param::PLACEMENT, dft::detail::config_value::INPLACE>(
        desc, "Unexpected value for placement");

    auto commit_handle = dft::detail::get_commit(desc);
    detail::check_bwd_commit(desc);
    sycl::queue &cpu_queue{ commit_handle->get_queue() };

    auto mklcpu_desc_buffer{ detail::get_buffer(commit_handle) };

    return cpu_queue.submit([&](sycl::handler &cgh) {
        auto desc_acc = mklcpu_desc_buffer.template get_access<sycl::access::mode::read>(cgh);
        cgh.depends_on(dependencies);
        detail::host_task<class host_usm_kernel_back_inplace>(cgh, [=]() {
            DFT_ERROR status = DftiComputeBackward(desc_acc[detail::DIR::bwd], inout);
            if (status != DFTI_NO_ERROR) {
                throw oneapi::mkl::exception(
                    "dft/backends/mklcpu", "compute_backward",
                    std::string("DftiComputeBackward failed : ") + DftiErrorMessage(status));
            }
        });
    });
}

//In-place transform, using config_param::COMPLEX_STORAGE=config_value::REAL_REAL data format
template <typename descriptor_type>
ONEMKL_EXPORT sycl::event compute_backward(descriptor_type &desc, scalar<descriptor_type> *inout_re,
                                           scalar<descriptor_type> *inout_im,
                                           const std::vector<sycl::event> &dependencies) {
    detail::expect_config<dft::detail::config_param::COMPLEX_STORAGE,
                          dft::detail::config_value::REAL_REAL>(
        desc, "Unexpected value for complex storage");
    auto commit_handle = dft::detail::get_commit(desc);
    detail::check_bwd_commit(desc);
    sycl::queue &cpu_queue{ commit_handle->get_queue() };

    auto mklcpu_desc_buffer{ detail::get_buffer(commit_handle) };

    return cpu_queue.submit([&](sycl::handler &cgh) {
        auto desc_acc = mklcpu_desc_buffer.template get_access<sycl::access::mode::read>(cgh);
        cgh.depends_on(dependencies);
        detail::host_task<class host_usm_kernel_split_back_inplace>(cgh, [=]() {
            DFT_ERROR status = DftiComputeBackward(desc_acc[detail::DIR::bwd], inout_re, inout_im);
            if (status != DFTI_NO_ERROR) {
                throw oneapi::mkl::exception(
                    "dft/backends/mklcpu", "compute_backward",
                    std::string("DftiComputeBackward failed : ") + DftiErrorMessage(status));
            }
        });
    });
}

//Out-of-place transform
template <typename descriptor_type>
ONEMKL_EXPORT sycl::event compute_backward(descriptor_type &desc, bwd<descriptor_type> *in,
                                           fwd<descriptor_type> *out,
                                           const std::vector<sycl::event> &dependencies) {
    // Check: inplace, complex storage
    detail::expect_config<dft::detail::config_param::PLACEMENT,
                          dft::detail::config_value::NOT_INPLACE>(desc,
                                                                  "Unexpected value for placement");

    auto commit_handle = dft::detail::get_commit(desc);
    detail::check_bwd_commit(desc);
    sycl::queue &cpu_queue{ commit_handle->get_queue() };

    auto mklcpu_desc_buffer{ detail::get_buffer(commit_handle) };
    return cpu_queue.submit([&](sycl::handler &cgh) {
        auto desc_acc = mklcpu_desc_buffer.template get_access<sycl::access::mode::read>(cgh);

        cgh.depends_on(dependencies);
        detail::host_task<class host_usm_kernel_back_outofplace>(cgh, [=]() {
            DFT_ERROR status = DftiComputeBackward(desc_acc[detail::DIR::bwd], in, out);
            if (status != DFTI_NO_ERROR) {
                throw oneapi::mkl::exception(
                    "dft/backends/mklcpu", "compute_backward",
                    std::string("DftiComputeBackward failed : ") + DftiErrorMessage(status));
            }
        });
    });
}

//Out-of-place transform, using config_param::COMPLEX_STORAGE=config_value::REAL_REAL data format
template <typename descriptor_type>
ONEMKL_EXPORT sycl::event compute_backward(descriptor_type &desc, scalar<descriptor_type> *in_re,
                                           scalar<descriptor_type> *in_im,
                                           scalar<descriptor_type> *out_re,
                                           scalar<descriptor_type> *out_im,
                                           const std::vector<sycl::event> &dependencies) {
    detail::expect_config<dft::detail::config_param::COMPLEX_STORAGE,
                          dft::detail::config_value::REAL_REAL>(
        desc, "Unexpected value for complex storage");
    auto commit_handle = dft::detail::get_commit(desc);
    detail::check_bwd_commit(desc);
    sycl::queue &cpu_queue{ commit_handle->get_queue() };

    auto mklcpu_desc_buffer{ detail::get_buffer(commit_handle) };
    return cpu_queue.submit([&](sycl::handler &cgh) {
        auto desc_acc = mklcpu_desc_buffer.template get_access<sycl::access::mode::read>(cgh);

        cgh.depends_on(dependencies);
        detail::host_task<class host_usm_kernel_split_back_outofplace>(cgh, [=]() {
            DFT_ERROR status =
                DftiComputeBackward(desc_acc[detail::DIR::bwd], in_re, in_im, out_re, out_im);
            if (status != DFTI_NO_ERROR) {
                throw oneapi::mkl::exception(
                    "dft/backends/mklcpu", "compute_backward",
                    std::string("DftiComputeBackward failed : ") + DftiErrorMessage(status));
            }
        });
    });
}

// Template function instantiations
#include "dft/backends/backend_backward_instantiations.cxx"

} // namespace oneapi::mkl::dft::mklcpu

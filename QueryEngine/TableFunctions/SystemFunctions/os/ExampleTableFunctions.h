/*
 * Copyright 2021 OmniSci, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "QueryEngine/OmniSciTypes.h"

#ifndef __CUDACC__

// clang-format off
/*
  UDTF: tf_mandelbrot__cpu_(TableFunctionManager, int32_t, int32_t, double, double, double, double, int32_t) -> Column<double> x, Column<double> y, Column<int32_t> num_iterations
*/
// clang-format on

EXTENSION_NOINLINE
int32_t tf_mandelbrot__cpu_(TableFunctionManager& mgr,
                            const int32_t x_pixels,
                            const int32_t y_pixels,
                            const double x_min,
                            const double x_max,
                            const double y_min,
                            const double y_max,
                            const int32_t max_iterations,
                            Column<double>& output_x,
                            Column<double>& output_y,
                            Column<int32_t>& output_num_iterations);

// clang-format off
/*
  UDTF: tf_mandelbrot_float__cpu_(TableFunctionManager, int32_t, int32_t, float, float, float, float, int32_t) -> Column<float> x, Column<float> y, Column<int32_t> num_iterations
*/
// clang-format on

EXTENSION_NOINLINE
int32_t tf_mandelbrot_float__cpu_(TableFunctionManager& mgr,
                                  const int32_t x_pixels,
                                  const int32_t y_pixels,
                                  const float x_min,
                                  const float x_max,
                                  const float y_min,
                                  const float y_max,
                                  const int32_t max_iterations,
                                  Column<float>& output_x,
                                  Column<float>& output_y,
                                  Column<int32_t>& output_num_iterations);

#else  // #ifndef __CUDACC__

// clang-format off
/*
  UDTF: tf_mandelbrot_cuda__gpu_(int32_t x_pixels, int32_t y_pixels, double, double, double, double, int32_t) ->
    Column<double> x, Column<double> y, Column<int32_t> num_iterations |
    output_row_size="x_pixels * y_pixels"
*/
// clang-format on

EXTENSION_NOINLINE
int32_t tf_mandelbrot_cuda__gpu_(const int32_t x_pixels,
                                 const int32_t y_pixels,
                                 const double x_min,
                                 const double x_max,
                                 const double y_min,
                                 const double y_max,
                                 const int32_t max_iterations,
                                 Column<double>& output_x,
                                 Column<double>& output_y,
                                 Column<int32_t>& output_num_iterations);

// clang-format off
/*
  UDTF: tf_mandelbrot_cuda_float__gpu_(int32_t x_pixels, int32_t y_pixels, float, float, float, float, int32_t) ->
    Column<float> x, Column<float> y, Column<int32_t> num_iterations |
    output_row_size="x_pixels * y_pixels"
*/
// clang-format on

EXTENSION_NOINLINE
int32_t tf_mandelbrot_cuda_float__gpu_(const int32_t x_pixels,
                                       const int32_t y_pixels,
                                       const float x_min,
                                       const float x_max,
                                       const float y_min,
                                       const float y_max,
                                       const int32_t max_iterations,
                                       Column<float>& output_x,
                                       Column<float>& output_y,
                                       Column<int32_t>& output_num_iterations);

#endif  // __CUDACC__
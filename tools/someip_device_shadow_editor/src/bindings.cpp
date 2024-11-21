// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DeviceShadowOverSomeipExampleApplication.hpp"
#include <new> // IWYU pragma: keep
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // IWYU pragma: keep
#include <type_traits>    // IWYU pragma: keep
#include <utility>        // IWYU pragma: keep

namespace py = pybind11;

PYBIND11_MODULE( someip_device_shadow_editor, m )
{
    py::class_<DeviceShadowOverSomeipExampleApplication>( m, "DeviceShadowOverSomeipExampleApplication" )
        .def( py::init<>() )
        .def( "init", &DeviceShadowOverSomeipExampleApplication::init )
        .def( "deinit", &DeviceShadowOverSomeipExampleApplication::deinit )
        .def( "get_shadow", &DeviceShadowOverSomeipExampleApplication::getShadow )
        .def( "update_shadow", &DeviceShadowOverSomeipExampleApplication::updateShadow )
        .def( "delete_shadow", &DeviceShadowOverSomeipExampleApplication::deleteShadow )
        .def( "get_instance", &DeviceShadowOverSomeipExampleApplication::getInstance );
}

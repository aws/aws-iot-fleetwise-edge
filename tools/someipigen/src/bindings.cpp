// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "SignalManager.hpp"

#include <boost/any.hpp>
#include <boost/type_index/type_index_facade.hpp>
#include <cstdint>
#include <new> // IWYU pragma: keep
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // IWYU pragma: keep
#include <stdexcept>
#include <string>
#include <type_traits> // IWYU pragma: keep
#include <utility>     // IWYU pragma: keep

namespace py = pybind11;

PYBIND11_MODULE( someipigen, m )
{
    py::class_<SignalManager>( m, "SignalManager" )
        .def( py::init<>() )
        .def( "set_value",
              []( SignalManager &sh, const std::string &signal, py::object value ) {
                  boost::any signal_initial_value = sh.get_value( signal );
                  boost::any converted_value;
                  if ( signal_initial_value.type() == typeid( int ) )
                  {
                      converted_value = value.cast<int>();
                  }
                  else if ( signal_initial_value.type() == typeid( int32_t ) )
                  {
                      converted_value = value.cast<int32_t>();
                  }
                  else if ( signal_initial_value.type() == typeid( uint32_t ) )
                  {
                      converted_value = value.cast<uint32_t>();
                  }
                  else if ( signal_initial_value.type() == typeid( int64_t ) )
                  {
                      converted_value = value.cast<int64_t>();
                  }
                  else if ( signal_initial_value.type() == typeid( uint64_t ) )
                  {
                      converted_value = value.cast<uint64_t>();
                  }
                  else if ( signal_initial_value.type() == typeid( float ) )
                  {
                      converted_value = value.cast<float>();
                  }
                  else if ( signal_initial_value.type() == typeid( double ) )
                  {
                      converted_value = value.cast<double>();
                  }
                  else if ( signal_initial_value.type() == typeid( std::string ) )
                  {
                      converted_value = value.cast<std::string>();
                  }
                  else if ( signal_initial_value.type() == typeid( bool ) )
                  {
                      converted_value = value.cast<bool>();
                  }
                  else
                  {
                      throw std::runtime_error( "Unsupported signal type for conversion from Python object." );
                  }
                  sh.set_value( signal, converted_value );
              } )
        .def( "get_value",
              []( const SignalManager &sh, const std::string &signal ) -> py::object {
                  boost::any value = sh.get_value( signal );
                  if ( value.type() == typeid( int ) )
                  {
                      return py::cast( boost::any_cast<int>( value ) );
                  }
                  else if ( value.type() == typeid( int32_t ) )
                  {
                      return py::cast( boost::any_cast<int32_t>( value ) );
                  }
                  else if ( value.type() == typeid( uint32_t ) )
                  {
                      return py::cast( boost::any_cast<uint32_t>( value ) );
                  }
                  else if ( value.type() == typeid( int64_t ) )
                  {
                      return py::cast( boost::any_cast<int64_t>( value ) );
                  }
                  else if ( value.type() == typeid( uint64_t ) )
                  {
                      return py::cast( boost::any_cast<uint64_t>( value ) );
                  }
                  else if ( value.type() == typeid( float ) )
                  {
                      return py::cast( boost::any_cast<float>( value ) );
                  }
                  else if ( value.type() == typeid( double ) )
                  {
                      return py::cast( boost::any_cast<double>( value ) );
                  }
                  else if ( value.type() == typeid( std::string ) )
                  {
                      return py::cast( boost::any_cast<std::string>( value ) );
                  }
                  else if ( value.type() == typeid( bool ) )
                  {
                      return py::cast( boost::any_cast<bool>( value ) );
                  }
                  else
                  {
                      throw std::runtime_error(
                          "Unsupported type stored in boost::any for conversion to Python object." );
                  }
              } )
        .def( "set_response_delay_ms_for_set", &SignalManager::set_response_delay_ms_for_set )
        .def( "set_response_delay_ms_for_get", &SignalManager::set_response_delay_ms_for_get )
        .def( "save_values", &SignalManager::save_values )
        .def( "load_values", &SignalManager::load_values )
        .def( "get_signals", &SignalManager::get_signals )
        .def( "get_instance", &SignalManager::get_instance )
        .def( "start", &SignalManager::start )
        .def( "stop", &SignalManager::stop )
        .def( "start",
              []( SignalManager &sh,
                  const std::string &domain,
                  const std::string &instance,
                  const std::string &connection ) {
                  sh.start( domain, instance, connection );
              } );
}

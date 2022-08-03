
/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "TraceModule.h"

#include <gtest/gtest.h>
#include <thread>

using namespace Aws::IoTFleetWise::Platform::Linux;

TEST( TraceModuleTest, TraceModulePrint )
{
    TraceModule::get().setVariable( TraceVariable::READ_SOCKET_FRAMES_0, 10 );
    TraceModule::get().setVariable( TraceVariable::READ_SOCKET_FRAMES_0, 20 );
    TraceModule::get().setVariable( TraceVariable::READ_SOCKET_FRAMES_0, 15 );

    TraceModule::get().sectionBegin( TraceSection::BUILD_MQTT );
    std::this_thread::sleep_for( std::chrono::milliseconds( 4 ) );
    TraceModule::get().sectionEnd( TraceSection::BUILD_MQTT );

    std::this_thread::sleep_for( std::chrono::milliseconds( 9 ) );

    TraceModule::get().sectionBegin( TraceSection::BUILD_MQTT );
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    TraceModule::get().sectionEnd( TraceSection::BUILD_MQTT );

    std::this_thread::sleep_for( std::chrono::milliseconds( 11 ) );

    TraceModule::get().sectionBegin( TraceSection::BUILD_MQTT );
    std::this_thread::sleep_for( std::chrono::milliseconds( 6 ) );
    TraceModule::get().sectionEnd( TraceSection::BUILD_MQTT );

    ASSERT_EQ( TraceModule::get().getVariableMax( TraceVariable::READ_SOCKET_FRAMES_0 ), 20 );
    TraceModule::get().print();
    TraceModule::get().startNewObservationWindow();
    TraceModule::get().print();
    TraceModule::get().startNewObservationWindow();
}

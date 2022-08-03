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

#pragma once

// Includes
#include "IDecoderDictionary.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

using namespace Aws::IoTFleetWise::DataManagement;

/**
 * @brief This is the listener interface to notify any listeners on change of Decoder Dictionary.
 *
 */
class IActiveDecoderDictionaryListener
{
public:
    /**
     * @brief default destructor
     */
    virtual ~IActiveDecoderDictionaryListener() = default;

    /**
     * @brief The callback function used to notify any listeners on change of Decoder Dictionary
     *
     * @param dictionary const shared pointer pointing to a constant decoder dictionary
     * @param networkProtocol network protocol type indicating which type of decoder dictionary it's updating
     * @return None
     */
    virtual void onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                             VehicleDataSourceProtocol networkProtocol ) = 0;
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
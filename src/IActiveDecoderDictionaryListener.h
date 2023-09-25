// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "IDecoderDictionary.h"

namespace Aws
{
namespace IoTFleetWise
{

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

} // namespace IoTFleetWise
} // namespace Aws

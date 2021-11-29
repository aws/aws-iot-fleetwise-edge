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

#include "CollectionInspectionAPITypes.h"
#include <random>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

/**
 * @brief Utility class used by CollectionInspectionEngine for reducing data sent from FWE to cloud
 *
 * One way to reduce data is to randomly not send out data. Other mechanisms to
 * reduce the data sent out are planned in the future.
 */
class DataReduction
{

public:
    DataReduction()
        : mDisableProbability( false )
        , mRandomDevice()
        , mRandomGenerator( mRandomDevice() )
        , mRandomUniformDistribution( MIN_PROBABILITY, MAX_PROBABILITY ){};

    /**
     * @brief probability data reduction can be disabled for example for debugging
     *
     * @param disableProbability if true all data will be sent out independent of the reduction defined
     */
    void
    setDisableProbability( bool disableProbability )
    {
        mDisableProbability = disableProbability;
    }

    /**
     * @brief decides if the data should be sent out or not partially based on randomness
     *
     * As random number generation is part of the function the result is not deterministic.
     * Depending on the input parameters the probability of this function returning true changes.
     *
     * @return non deterministic decision to send the data out or not
     *
     */
    bool
    shallSendData( double collectionSchemeProbability )
    {
        return ( mDisableProbability ||
                 ( collectionSchemeProbability > 0.0 && generateNewRandomNumber() <= collectionSchemeProbability ) );
    }

private:
    bool mDisableProbability;
    /* Pseudo random number generation*/
    std::random_device mRandomDevice; // Will be used to obtain a seed for the random number engine
    std::mt19937 mRandomGenerator;    // Standard mersenne_twister_engine seeded with rd()
    std::uniform_real_distribution<> mRandomUniformDistribution;

    double
    generateNewRandomNumber()
    {
        return this->mRandomUniformDistribution( this->mRandomGenerator );
    };
};

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
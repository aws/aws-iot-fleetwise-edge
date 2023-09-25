// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CollectionInspectionAPITypes.h"
#include <random>

namespace Aws
{
namespace IoTFleetWise
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
        return ( mDisableProbability || ( ( collectionSchemeProbability > 0.0 ) &&
                                          ( generateNewRandomNumber() <= collectionSchemeProbability ) ) );
    }

private:
    bool mDisableProbability{ false };
    /* Pseudo random number generation*/
    std::mt19937 mRandomGenerator{ std::random_device{}() }; // Standard mersenne_twister_engine seeded with rd()
    std::uniform_real_distribution<> mRandomUniformDistribution{ MIN_PROBABILITY, MAX_PROBABILITY };

    double
    generateNewRandomNumber()
    {
        return this->mRandomUniformDistribution( this->mRandomGenerator );
    };
};

} // namespace IoTFleetWise
} // namespace Aws

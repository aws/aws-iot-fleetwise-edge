# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

import json


class Histogram:
    def __init__(self, min_val, max_val, num_bins):
        self.min_val = min_val
        self.max_val = max_val
        self.num_bins = num_bins
        self.reset()

    def calc(self, val):
        if val <= self.min_val:
            bin_idx = 0
        elif val >= self.max_val:
            bin_idx = self.num_bins - 1
        else:
            bin_idx = int(
                (val - self.min_val) * (self.num_bins - 1) / (self.max_val - self.min_val)
            )
        self.bins[bin_idx] += 1
        self.sample_count += 1

    def reset(self):
        self.sample_count = 0
        self.bins = [0] * self.num_bins


hist = Histogram(min_val=-1000, max_val=1000, num_bins=100)


def invoke(val):
    global hist
    if val is None:  # Ignore undefined values
        return False
    hist.calc(val)

    # Collect data every 1000 samples:
    if hist.sample_count >= 1000:
        json_result = json.dumps(hist.bins)
        hist.reset()

        # Collected data can be returned as the second value in a tuple:
        return True, {"Vehicle.Histogram": json_result}

    return False

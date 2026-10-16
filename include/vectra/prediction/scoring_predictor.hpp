// Transparent scoring predictor: maps observable features to a
// continuation / reversal / neutral candidate.
//
// The model is deliberately simple and fully documented in
// docs/mathematical_model.md so that it can be falsified. It contains no
// machine learning, no fitted parameters and no claim of calibration.
#pragma once

#include "vectra/behavior/behavior_engine.hpp"
#include "vectra/prediction/prediction.hpp"

namespace vectra::prediction {

class ScoringPredictor {
 public:
  explicit ScoringPredictor(PredictionConfig config);

  Prediction predict(const behavior::BehaviorFeatures& features) const;

  const PredictionConfig& config() const { return config_; }

 private:
  PredictionConfig config_;
};

}  // namespace vectra::prediction

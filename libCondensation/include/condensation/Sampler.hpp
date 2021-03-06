/*
 * Sampler.hpp
 *
 *  Created on: 15.08.2012
 *      Author: poschmann
 */

#ifndef SAMPLER_HPP_
#define SAMPLER_HPP_

#include "opencv2/core/core.hpp"
#include <vector>
#include <memory>

using cv::Mat;
using std::vector;
using std::shared_ptr;

namespace condensation {

class Sample;

/**
 * Creates new samples.
 */
class Sampler {
public:

	virtual ~Sampler() {}

	/**
	 * Initializes this sampler.
	 *
	 * @param[in] image The current image.
	 */
	virtual void init(const Mat& image) = 0;

	/**
	 * Creates new samples.
	 *
	 * @param[in] samples The vector containing the samples of the previous time step.
	 * @param[in,out] newSamples The vector to insert the new samples into.
	 * @param[in] image The new image.
	 * @param[in] target The previous target state.
	 */
	virtual void sample(const vector<shared_ptr<Sample>>& samples, vector<shared_ptr<Sample>>& newSamples,
			const Mat& image, const shared_ptr<Sample> target) = 0;
};

} /* namespace condensation */
#endif /* SAMPLER_HPP_ */

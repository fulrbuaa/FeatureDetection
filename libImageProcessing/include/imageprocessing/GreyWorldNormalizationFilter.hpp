/*
 * GreyWorldNormalizationFilter.hpp
 *
 *  Created on: 30.07.2013
 *      Author: poschmann
 */

#ifndef GREYWORLDNORMALIZATIONFILTER_HPP_
#define GREYWORLDNORMALIZATIONFILTER_HPP_

#include "imageprocessing/ImageFilter.hpp"

namespace imageprocessing {

/**
 * Image filter that does a grey world normalization. Expects the images to have three channels of uchar data
 * (image type is CV_8UC3). The output image will have the same type.
 */
class GreyWorldNormalizationFilter : public ImageFilter {
public:

	/**
	 * Constructs a new grey world normalization filter.
	 */
	GreyWorldNormalizationFilter();

	~GreyWorldNormalizationFilter();

	using ImageFilter::applyTo;

	Mat applyTo(const Mat& image, Mat& filtered) const;

	void applyInPlace(Mat& image) const;
};

} /* namespace imageprocessing */
#endif /* GREYWORLDNORMALIZATIONFILTER_HPP_ */

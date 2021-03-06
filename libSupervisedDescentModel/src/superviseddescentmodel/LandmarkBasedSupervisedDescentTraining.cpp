/*
 * LandmarkBasedSupervisedDescentTraining.cpp
 *
 *  Created on: 04.02.2014
 *      Author: Patrik Huber
 */

#include "superviseddescentmodel/LandmarkBasedSupervisedDescentTraining.hpp"
#include "logging/LoggerFactory.hpp"

#include <fstream>
#include <random>
#include <chrono>

#include "opencv2/imgproc/imgproc.hpp"
#include "Eigen/Dense"

#ifdef WIN32
	#define BOOST_ALL_DYN_LINK	// Link against the dynamic boost lib. Seems to be necessary because we use /MD, i.e. link to the dynamic CRT.
	#define BOOST_ALL_NO_LIB	// Don't use the automatic library linking by boost with VS2010 (#pragma ...). Instead, we specify everything in cmake.
#endif
#include "boost/filesystem/path.hpp"

using logging::Logger;
using logging::LoggerFactory;
using cv::Mat;
using cv::Rect;
using cv::Scalar;
using std::string;

namespace superviseddescentmodel {

void LandmarkBasedSupervisedDescentTraining::saveShapeInstanceToMLtxt(Mat shapeInstance, string filename)
{
	int numLandmarks;
	if (shapeInstance.rows > 1) {
		numLandmarks = shapeInstance.rows / 2;
	}
	else {
		numLandmarks = shapeInstance.cols / 2;
	}
	std::ofstream myfile;
	myfile.open(filename);
	myfile << "x = [";
	for (int i = 0; i < numLandmarks; ++i) {
		myfile << shapeInstance.at<float>(i) << ", ";
	}
	myfile << "];" << std::endl << "y = [";
	for (int i = 0; i < numLandmarks; ++i) {
		myfile << shapeInstance.at<float>(i + numLandmarks) << ", ";
	}
	myfile << "];" << std::endl;
	myfile.close();
}

Mat LandmarkBasedSupervisedDescentTraining::transformLandmarksNormalized(Mat landmarks, Rect box)
{
	// Todo: Quick doc
	Mat transformed(landmarks.rows, landmarks.cols, landmarks.type());
	int numLandmarks = landmarks.cols / 2;
	for (int i = 0; i < numLandmarks; ++i) { // Todo: This could be done without a for-loop I think, by splitting landmarks in a x- and y-Mat
		float normalizedX = ((landmarks.at<float>(i)                - box.x) / static_cast<float>(box.width)) - 0.5f;
		float normalizedY = ((landmarks.at<float>(i + numLandmarks) - box.y) / static_cast<float>(box.height)) - 0.5f;
		transformed.at<float>(i) = normalizedX;
		transformed.at<float>(i + numLandmarks) = normalizedY;
	}
	return transformed;
}

cv::Mat LandmarkBasedSupervisedDescentTraining::alignMean(cv::Mat mean, cv::Rect faceBox, float scalingX/*=1.0f*/, float scalingY/*=1.0f*/, float translationX/*=0.0f*/, float translationY/*=0.0f*/)
{
	// Initial estimate x_0: Center the mean face at the [-0.5, 0.5] x [-0.5, 0.5] square (assuming the face-box is that square)
	// More precise: Take the mean as it is (assume it is in a space [-0.5, 0.5] x [-0.5, 0.5]), and just place it in the face-box as
	// if the box is [-0.5, 0.5] x [-0.5, 0.5]. (i.e. the mean coordinates get upscaled)
	Mat alignedMean = mean.clone();
	Mat alignedMeanX = alignedMean.colRange(0, alignedMean.cols / 2);
	Mat alignedMeanY = alignedMean.colRange(alignedMean.cols / 2, alignedMean.cols);
	alignedMeanX = (alignedMeanX*scalingX + 0.5f + translationX) * faceBox.width + faceBox.x;
	alignedMeanY = (alignedMeanY*scalingY + 0.5f + translationY) * faceBox.height + faceBox.y;
	return alignedMean;

	/* with variances: (old code, untested)
	initialShapeEstimate2X0_x = (initialShapeEstimate2X0_x * delta_sx + 0.5f) * detectedFace.width + detectedFace.x + delta_tx;
	initialShapeEstimate2X0_y = (initialShapeEstimate2X0_y * delta_sy + 0.5f) * detectedFace.height + detectedFace.y + delta_ty;
	*/
}


cv::Mat LandmarkBasedSupervisedDescentTraining::meanNormalizationUnitSumSquaredNorms(cv::Mat modelMean)
{
	int numLandmarks = modelMean.cols / 2;
	Mat modelMeanX = modelMean.colRange(0, numLandmarks);
	Mat modelMeanY = modelMean.colRange(numLandmarks, 2 * numLandmarks);
	// calculate the centroid:
	Scalar cx = cv::mean(modelMeanX);
	Scalar cy = cv::mean(modelMeanY);
	// move all points to the centroid
	modelMeanX = modelMeanX - cx[0];
	modelMeanY = modelMeanY - cy[0];
	// scale so that the average norm is 1/numLandmarks (i.e.: the total norm of all vectors added is 1).
	// note: that doesn't make too much sense, because it follows that the more landmarks we use, the smaller the mean-face will be.
	float currentTotalSquaredNorm = 0.0f;
	for (int p = 0; p < numLandmarks; ++p) {
		float x = modelMeanX.at<float>(p);
		float y = modelMeanY.at<float>(p);
		currentTotalSquaredNorm += (x*x + y*y);
	}
	// multiply every vectors coordinate by the sqrt of the currentTotalSquaredNorm
	modelMean /= std::sqrt(currentTotalSquaredNorm);
	return modelMean;
}

cv::Mat LandmarkBasedSupervisedDescentTraining::calculateMean(std::vector<cv::Mat> landmarks, AlignGroundtruth alignGroundtruth, MeanNormalization meanNormalization, std::vector<cv::Rect> faceboxes/*=std::vector<cv::Rect>()*/)
{
	Logger logger = Loggers->getLogger("superviseddescentmodel");
	if (landmarks.size() < 1) {
		string msg("No landmarks provided to calculate the mean.");
		logger.error(msg);
		throw std::runtime_error(msg);
	}
	// Calculate the mean-shape of all training images:
	// 1) Prepare the ground-truth landmarks:
	//    Note: We could do some Procrustes and align all shapes to some calculated mean-shape. But actually just the mean calculated like this is a good approximation.
	Mat landmarksMatrix(landmarks.size(), landmarks[0].cols, CV_32FC1);
	switch (alignGroundtruth)
	{
	case AlignGroundtruth::NONE: // from vector<Mat> to one Mat. Just copy, no transformation.
		for (auto currentImage = 0; currentImage < landmarks.size(); ++currentImage) {
			Mat groundtruthLms = landmarks[currentImage];
			groundtruthLms.copyTo(landmarksMatrix.row(currentImage));
		}
		break;
	case AlignGroundtruth::NORMALIZED_FACEBOX:
		if (faceboxes.size() != landmarks.size()) {
			string msg("'AlignGroundtruth' is set to NORMALIZED_FACEBOX but faceboxes.size() != landmarks.size(). Please provide a face-box for each set of landmarks.");
			logger.error(msg);
			throw std::runtime_error(msg);
		}
		for (auto currentImage = 0; currentImage < landmarks.size(); ++currentImage) {
			Mat transformed = transformLandmarksNormalized(landmarks[currentImage], faceboxes[currentImage]);
			transformed.copyTo(landmarksMatrix.row(currentImage));
		}
		break;
	default:
		string msg("'AlignGroundtruth' is set to an unknown value. This should not happen.");
		logger.error(msg);
		throw std::runtime_error(msg);
		break;
	}

	// 2) Take the mean of every row:
	Mat modelMean;
	cv::reduce(landmarksMatrix, modelMean, 0, CV_REDUCE_AVG); // reduce to 1 row

	// 3) Optional: Normalize the calculated mean by something (e.g. make it have a sum of squared norms of 1, as in the original SDM paper)
	switch (meanNormalization)
	{
	case MeanNormalization::NONE:
		break;
	case MeanNormalization::UNIT_SUM_SQUARED_NORMS:
		modelMean = meanNormalizationUnitSumSquaredNorms(modelMean);
		break;
	default:
		string msg("'MeanNormalization' is set to an unknown value. This should not happen.");
		logger.error(msg);
		throw std::runtime_error(msg);
		break;
	}
	saveShapeInstanceToMLtxt(modelMean, "mean.txt");
	return modelMean;
}

LandmarkBasedSupervisedDescentTraining::ModelVariance LandmarkBasedSupervisedDescentTraining::calculateModelVariance(vector<Mat> trainingImages, vector<cv::Rect> trainingFaceboxes, Mat groundtruthLandmarks, Mat initialShapeEstimateX0)
{
	Mat delta_tx(trainingImages.size(), 1, CV_32FC1);
	Mat delta_ty(trainingImages.size(), 1, CV_32FC1);
	Mat delta_sx(trainingImages.size(), 1, CV_32FC1);
	Mat delta_sy(trainingImages.size(), 1, CV_32FC1);
	int numModelLandmarks = groundtruthLandmarks.cols / 2;
	int numDataPerTrainingImage = initialShapeEstimateX0.rows / groundtruthLandmarks.rows;
	int currentImage = 0;
	for (auto currentImage = 0; currentImage < trainingImages.size(); ++currentImage) {
		Mat img = trainingImages[currentImage];
		cv::Rect detectedFace = trainingFaceboxes[currentImage]; // Caution: Depending on flags selected earlier, we might not have detected faces yet!

		// calculate the centroid and the min-max bounding-box (for the width/height) of the ground-truth and the initial estimate x0:
		Mat groundtruth_x = groundtruthLandmarks.row(currentImage).colRange(0, numModelLandmarks);
		Mat groundtruth_y = groundtruthLandmarks.row(currentImage).colRange(numModelLandmarks, numModelLandmarks * 2);
		
		Mat initialShapeEstimateX0_x = initialShapeEstimateX0.row(currentImage * numDataPerTrainingImage).colRange(0, initialShapeEstimateX0.cols / 2);
		Mat initialShapeEstimateX0_y = initialShapeEstimateX0.row(currentImage * numDataPerTrainingImage).colRange(initialShapeEstimateX0.cols / 2, initialShapeEstimateX0.cols);

		delta_tx.at<float>(currentImage) = calculateTranslationVariance(groundtruth_x, initialShapeEstimateX0_x, detectedFace.width); // This is in relation to the V&J face-box
		delta_ty.at<float>(currentImage) = calculateTranslationVariance(groundtruth_y, initialShapeEstimateX0_y, detectedFace.height);
		
		delta_sx.at<float>(currentImage) = calculateScaleVariance(groundtruth_x, initialShapeEstimateX0_x);
		delta_sy.at<float>(currentImage) = calculateScaleVariance(groundtruth_y, initialShapeEstimateX0_y);

		// Note: The tx we measure is the bias of the face-detector (and should be 0 in an ideal world).
		// We could also use abs(gt-x0)/fb.w instead, what we would then measure is how far the x0 is away
		// from the gt.
		// Actually we should kind of measure both: Correct for the first, and use the latter for the sampling. TODO!!! (MeanNormalization=unitnorm... or FB)
		// However, in the paper they only give variances (for the tracking case), not mu's, assuming the mean is 0 and 1?
		// After long deliberation, I think it's a lost cause - what we measure is just not what is described and what 
		// we can find a reasonable explanation for augmenting training data.
		// Solution: Just try both: Perturb the GT and perturb the x0 (with mu's zero) with the measured variance.
		// NO!!! Perturb the GT by +-mu (mu!=0) makes no sense at all in detection! (maybe in tracking?)
		
		// Wording in paper (?): Calculate the mean and variances of the translational and scaling differences between the initial and true landmark locations:
	}
	// Calculate the mean/variances and store them:
	ModelVariance modelVariance;
	Mat mmu_t_x, mmu_t_y, mmu_s_x, mmu_s_y, msigma_t_x, msigma_t_y, msigma_s_x, msigma_s_y;
	cv::meanStdDev(delta_tx, mmu_t_x, msigma_t_x);
	cv::meanStdDev(delta_ty, mmu_t_y, msigma_t_y);
	cv::meanStdDev(delta_sx, mmu_s_x, msigma_s_x);
	cv::meanStdDev(delta_sy, mmu_s_y, msigma_s_y);
	modelVariance.tx.mu = mmu_t_x.at<double>(0);
	modelVariance.ty.mu = mmu_t_y.at<double>(0);
	modelVariance.sx.mu = mmu_s_x.at<double>(0);
	modelVariance.sy.mu = mmu_s_y.at<double>(0);
	modelVariance.tx.sigma = msigma_t_x.at<double>(0);
	modelVariance.ty.sigma = msigma_t_y.at<double>(0);
	modelVariance.sx.sigma = msigma_s_x.at<double>(0);
	modelVariance.sy.sigma = msigma_s_y.at<double>(0);

	return modelVariance;
}

std::pair<Mat, LandmarkBasedSupervisedDescentTraining::ModelVariance> LandmarkBasedSupervisedDescentTraining::rescaleModel(Mat modelMean, ModelVariance modelVariance)
{
	Mat modelMean_x = modelMean.colRange(0, modelMean.cols / 2);
	Mat modelMean_y = modelMean.colRange(modelMean.cols / 2, modelMean.cols);
	modelMean_x = (modelMean_x * modelVariance.sx.mu) + modelVariance.tx.mu;
	modelMean_y = (modelMean_y * modelVariance.sy.mu) + modelVariance.ty.mu;
	modelVariance.sx.sigma = modelVariance.sx.sigma * modelVariance.sx.mu;
	modelVariance.sx.mu = 1.0;
	modelVariance.sy.sigma = modelVariance.sy.sigma * modelVariance.sy.mu;
	modelVariance.sy.mu = 1.0;
	modelVariance.tx.mu = 0;
	modelVariance.ty.mu = 0;
	return std::make_pair(modelMean, modelVariance);
}

Mat LandmarkBasedSupervisedDescentTraining::putInDataAndGenerateSamples(vector<Mat> trainingImages, vector<cv::Rect> trainingFaceboxes, Mat modelMean, Mat initialShape, ModelVariance modelVariance, int numSamplesPerImage)
{
	std::mt19937 engine; ///< A Mersenne twister MT19937 engine
	engine.seed();
	std::normal_distribution<float> rndN_t_x(modelVariance.tx.mu, modelVariance.tx.sigma);
	std::normal_distribution<float> rndN_t_y(modelVariance.ty.mu, modelVariance.ty.sigma);
	GaussParameter scaleVariance;
	scaleVariance.mu = (modelVariance.sx.mu + modelVariance.sy.mu) / 2.0;
	scaleVariance.sigma = (modelVariance.sx.sigma + modelVariance.sy.sigma) / 2.0;
	std::normal_distribution<float> rndN_scale(scaleVariance.mu, scaleVariance.sigma);

	for (auto currentImage = 0; currentImage < trainingImages.size(); ++currentImage) {
		Mat img = trainingImages[currentImage];
		cv::Rect detectedFace = trainingFaceboxes[currentImage]; // Caution: Depending on flags selected earlier, we might not have detected faces yet!
		// Align the model to the current face-box. (rigid, only centering of the mean). x_0
		Mat initialShapeEstimateX0 = alignMean(modelMean, detectedFace);
		initialShapeEstimateX0.copyTo(initialShape.row(currentImage * (numSamplesPerImage + 1)));
		int numModelLandmarks = modelMean.cols / 2;
		for (int i = 0; i < numModelLandmarks; ++i) {
			//cv::circle(img, cv::Point2f(initialShapeEstimateX0.at<float>(i), initialShapeEstimateX0.at<float>(i + numModelLandmarks)), 2, Scalar(255.0f, 0.0f, 0.0f));
		}
		//cv::rectangle(img, detectedFace, Scalar(0.0f, 0.0f, 255.0f));
		// c) Generate Monte Carlo samples? With what variance? x_0^i (maybe make this step 3.)
		// sample around initialShapeThis, store in initialShape
		//		Save the samples, all in a matrix
		//		Todo 1) don't use pixel variance, but a scale-independent one (normalize by IED?)
		//			 2) calculate the variance from data (gt facebox?)
		for (int sample = 0; sample < numSamplesPerImage; ++sample) {
			double rndScale = rndN_scale(engine);
			Mat sampleShape = alignMean(modelMean, detectedFace, rndScale, rndScale, rndN_t_x(engine), rndN_t_y(engine));
			sampleShape.copyTo(initialShape.row(currentImage*(numSamplesPerImage + 1) + (sample + 1)));

			// Check if the sample goes outside the feature-extractable region?
			// TODO: The scaling needs to be done in the normalized facebox region?? Try to write it down?
			// Better do the translation in the norm-FB as well to be independent of face-size? yes we do that now. Check the Detection-code though!
			for (int i = 0; i < numModelLandmarks; ++i) {
				//cv::circle(img, cv::Point2f(sampleShape.at<float>(i), sampleShape.at<float>(i + numModelLandmarks)), 2, Scalar(0.0f, 0.0f, 255.0f));
			}
		}
	}

	return initialShape;
}

SdmLandmarkModel LandmarkBasedSupervisedDescentTraining::train(vector<Mat> trainingImages, vector<Mat> trainingGroundtruthLandmarks, vector<cv::Rect> trainingFaceboxes /*maybe optional bzw weglassen hier?*/, std::vector<string> modelLandmarks, vector<string> descriptorTypes, vector<shared_ptr<DescriptorExtractor>> descriptorExtractors)
{
	Logger logger = Loggers->getLogger("superviseddescentmodel");
	std::chrono::time_point<std::chrono::system_clock> start, end;
	int elapsed_mseconds;

	// TODO: Don't initialize with numSamples... Instead, push_back. Because
	// Sampling params: DiscardX0Sample. 
	// 1) PERTURB_GT, 2) PERTURB_(X0|DETECTOR_ESTIMATE)
	// NO!!! Perturb the GT by +-mu (mu!=0) makes no sense at all in detection! (maybe in tracking?)

	// Note: In tracking, i can't calculate the mu/sigma normalized wrt the FD face-box anymore. but pixel doesnt make sense either. So use the ied or min/max of the model. (ied bad idea, might not have eyes)

	ModelVariance modelVariance;

	int numImages = trainingImages.size();
	int numModelLandmarks = modelLandmarks.size();

	Mat groundtruthLandmarks(numImages, 2 * numModelLandmarks, CV_32FC1);
	// Just copy from vector<Mat> to one big Mat:
	for (auto currentImage = 0; currentImage < trainingGroundtruthLandmarks.size(); ++currentImage) {
		Mat groundtruthLms = trainingGroundtruthLandmarks[currentImage];
		groundtruthLms.copyTo(groundtruthLandmarks.row(currentImage));
	}

	Mat modelMean = calculateMean(trainingGroundtruthLandmarks, alignGroundtruth, meanNormalization, trainingFaceboxes);

	// Do the initial alignment: (different methods? depending if mean normalized or not?)
	Mat initialShape = Mat::zeros((numSamplesPerImage + 1) * trainingImages.size(), 2 * numModelLandmarks, CV_32FC1); // 10 samples + the original data = 11
	// aligns mean + fb to be x0. Note: fills in a matrix that's bigger (i.e. numSamplesPerImage as big)
	for (auto currentImage = 0; currentImage < trainingImages.size(); ++currentImage) {
		cv::Rect detectedFace = trainingFaceboxes[currentImage]; // Caution: Depending on flags selected earlier, we might not have detected faces yet!
		Mat initialShapeEstimateX0 = alignMean(modelMean, detectedFace);
		initialShapeEstimateX0.copyTo(initialShape.row(currentImage * (numSamplesPerImage + 1)));
		/* output
		Mat img = trainingImages[currentImage];
		for (int i = 0; i < numModelLandmarks; ++i) {
		cv::circle(img, cv::Point2f(initialShapeEstimateX0.at<float>(0, i), initialShapeEstimateX0.at<float>(0, i + numModelLandmarks)), 3, Scalar(255.0f, 0.0f, 0.0f));
		}
		cv::rectangle(img, detectedFace, Scalar(0.0f, 0.0f, 255.0f)); */
	}


	// Calculate the mean and variances of the translational and scaling differences between the initial and true landmark locations. (used for generating the samples)
	// This also includes the scaling/translation necessary to go from the unit-sqnorm normalized mean to one in a reasonably sized one w.r.t. the face-box.
	// This means we have to divide the stddev we draw by 2. The translation is ok though.
	// Todo: We should directly learn a reasonably normalized mean during the training!

	// flag NormalizeMeanAfterDeviationCalculation ?

	modelVariance = calculateModelVariance(trainingImages, trainingFaceboxes, groundtruthLandmarks, initialShape);

	// Rescale the model-mean, and the mean variances as well: (only necessary if our mean is not normalized to V&J face-box directly in first steps)
	// TODO add a flag?
	std::pair<Mat, ModelVariance> pp = rescaleModel(modelMean, modelVariance);
	modelMean = pp.first;
	modelVariance = pp.second;

	saveShapeInstanceToMLtxt(modelMean, "mean.txt");

	// 3. For every training image:
	// Store the initial shape estimate (x_0) of the image (using the rescaled mean), plus generate 10 samples and store them as well

	initialShape = putInDataAndGenerateSamples(trainingImages, trainingFaceboxes, modelMean, initialShape, modelVariance, numSamplesPerImage);

	// 4. For every sample plus the original image: (loop through the matrix)
	//			a) groundtruthShape, initialShape
	//				deltaShape = ground - initial
	// Duplicate each row in groundtruthLandmarks for every sample, store in groundtruthShapes
	Mat groundtruthShapes = Mat::zeros((numSamplesPerImage + 1) * trainingImages.size(), 2 * numModelLandmarks, CV_32FC1); // 10 samples + the original data = 11
	groundtruthShapes = duplicateGroundtruthShapes(groundtruthLandmarks, numSamplesPerImage);

	// We START here with the real algorithm, everything before was data preparation and calculation of the mean

	std::vector<cv::Mat> regressorData; // output

	// Prepare the data for the first cascade step learning. Starting from the mean initialization x0, deltaShape = gt - x0
	Mat deltaShape = groundtruthShapes - initialShape;
	// Calculate and print our starting error:
	double avgErrx0 = cv::norm(deltaShape, cv::NORM_L1) / (deltaShape.rows * deltaShape.cols); // TODO: Doesn't say much, need to normalize by IED! But maybe not at training time, should work with all landmarks
	logger.debug("LM-SDM training: Average pixel error starting from the mean initialization: " + lexical_cast<string>(avgErrx0));

	// 6. Learn a regressor for every cascade step
	for (int currentCascadeStep = 0; currentCascadeStep < numCascadeSteps; ++currentCascadeStep) {
		logger.debug("LM-SDM training: Training regressor " + lexical_cast<string>(currentCascadeStep));
		// b) Extract the features at all landmark locations initialShape (Paper: SIFT, 32x32 (?))
		//int featureDimension = 128;
		Mat featureMatrix;// = Mat::ones(initialShape.rows, (featureDimension * numModelLandmarks) + 1, CV_32FC1); // Our 'A'. The last column stays all 1's; it's for learning the offset/bias
		start = std::chrono::system_clock::now();
		int currentImage = 0;
		for (const auto& image : trainingImages) {
			Mat img = image;
			for (int sample = 0; sample < numSamplesPerImage + 1; ++sample) {
				vector<cv::Point2f> keypoints;
				for (int lm = 0; lm < numModelLandmarks; ++lm) {
					float px = initialShape.at<float>(currentImage*(numSamplesPerImage + 1) + sample, lm);
					float py = initialShape.at<float>(currentImage*(numSamplesPerImage + 1) + sample, lm + numModelLandmarks);
					keypoints.emplace_back(cv::Point2f(px, py));
				}
				Mat featureDescriptors = descriptorExtractors[currentCascadeStep]->getDescriptors(img, keypoints);
				// concatenate all the descriptors for this sample horizontally (into a row-vector)
				featureDescriptors = featureDescriptors.reshape(0, featureDescriptors.cols * numModelLandmarks).t();
				featureMatrix.push_back(featureDescriptors);
			}
			++currentImage;
		}
		Mat biasColumn = Mat::ones(initialShape.rows, 1, CV_32FC1);
		cv::hconcat(featureMatrix, biasColumn, featureMatrix); // Other options: 1) Generate one bigger Mat and use copyTo (memory would be continuous then) or 2) implement a FeatureDescriptorExtractor::getDimension()
		end = std::chrono::system_clock::now();
		elapsed_mseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
		logger.debug("Total time for extracting the feature descriptors: " + lexical_cast<string>(elapsed_mseconds)+"ms.");

		// 5. Add one row to the features (already done), add regLambda
		start = std::chrono::system_clock::now();
		Mat AtA = featureMatrix.t() * featureMatrix;
		float lambda;
		if (regularisation.regulariseWithEigenvalueThreshold) {
			std::chrono::time_point<std::chrono::system_clock> eigenTimeStart = std::chrono::system_clock::now();
			if (!AtA.isContinuous()) {
				std::string msg("Matrix is not continuous. This should not happen as we allocate it directly.");
				logger.error(msg);
				throw std::runtime_error(msg);
			}
			// Calculate the eigenvalues of AtA. This is only for output purposes and not needed. Would make sense to remove this as it might be time-consuming.
			Eigen::Map<Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> AtA_Eigen(AtA.ptr<float>(), AtA.rows, AtA.cols);
			Eigen::SelfAdjointEigenSolver<Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> es(AtA_Eigen);
			logger.trace("Smallest eigenvalue of AtA: " + lexical_cast<string>(es.eigenvalues()[0]));

			Eigen::FullPivLU<Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> luOfAtA(AtA_Eigen);
			logger.trace("Rank of AtA: " + lexical_cast<string>(luOfAtA.rank()));
			if (luOfAtA.isInvertible()) {
				logger.trace("AtA is invertible.");
			}
			else {
				logger.trace("AtA is not invertible.");
			}
			// Automatically set lambda: 
			// $ abs(luOfAtA.maxPivot()) * luOfAtA.threshold() $ is the threshold that Eigen uses to calculate the rank of the matrix. 
			// I.e.: Every eigenvalue above abs(lu.maxPivot()) * lu.threshold() is considered non-zero (i.e. to contribute to the rank of the matrix). See their documentation for more details.
			// We multiply it by 2 to make sure the matrix is invertible afterwards. (But: See Todo(1) below for a potential bug.)
			lambda = 2 * abs(luOfAtA.maxPivot()) * luOfAtA.threshold();

			std::chrono::time_point<std::chrono::system_clock> eigenTimeEnd = std::chrono::system_clock::now();
			elapsed_mseconds = std::chrono::duration_cast<std::chrono::milliseconds>(eigenTimeEnd - eigenTimeStart).count();
			logger.debug("Automatic calculation of lambda for AtA took " + lexical_cast<string>(elapsed_mseconds)+"ms.");
		}
		else
		{
			lambda = regularisation.factor * cv::norm(AtA) / (numImages*(numSamplesPerImage + 1)); // We divide by the number of images.
			// However, division by (AtA.rows * AtA.cols) might make more sense? Because this would be an approximation for the
			// RMS (eigenvalue? see sheet of paper, ev's of diag-matrix etc.), and thus our (conservative?) guess for a lambda that makes AtA invertible.
		}
		logger.debug("Setting lambda to: " + lexical_cast<string>(lambda));

		Mat regulariser = Mat::eye(AtA.rows, AtA.rows, CV_32FC1) * lambda;
		if (!regularisation.regulariseAffineComponent) {
			regulariser.at<float>(regulariser.rows - 1, regulariser.cols - 1) = 0.0f; // no lambda for the bias
		}
		//		solve for x!
		Mat AtAReg = AtA + regulariser;
		if (!AtAReg.isContinuous()) {
			std::string msg("Matrix is not continuous. This should not happen as we allocate it directly.");
			logger.error(msg);
			throw std::runtime_error(msg);
		}
		Eigen::Map<Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> AtAReg_Eigen(AtAReg.ptr<float>(), AtAReg.rows, AtAReg.cols);
		std::chrono::time_point<std::chrono::system_clock> inverseTimeStart = std::chrono::system_clock::now();
		// Calculate the full-pivoting LU decomposition of the regularized AtA. Note: We could also try FullPivHouseholderQR if our system is non-minimal (i.e. there are more constraints than unknowns).
		Eigen::FullPivLU<Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> luOfAtAReg(AtAReg_Eigen);
		// we could also print the smallest eigenvalue here, but that would take time to calculate (SelfAdjointEigenSolver, see above)
		float rankOfAtAReg = luOfAtAReg.rank();
		logger.trace("Rank of the regularized AtA: " + lexical_cast<string>(rankOfAtAReg));
		if (luOfAtAReg.isInvertible()) {
			logger.debug("The regularized AtA is invertible.");
		}
		else {
			// Eigen will most likely return garbage here (according to their docu anyway). We have a few options:
			// - Increase lambda
			// - Calculate the pseudo-inverse. See: http://eigen.tuxfamily.org/index.php?title=FAQ#Is_there_a_method_to_compute_the_.28Moore-Penrose.29_pseudo_inverse_.3F
			string msg("The regularized AtA is not invertible (its rank is " + lexical_cast<string>(rankOfAtAReg)+", full rank would be " + lexical_cast<string>(AtAReg_Eigen.rows()) + "). Increase lambda (or use the pseudo-inverse, which is not implemented yet).");
			logger.error(msg);
			#ifndef _DEBUG
				//throw std::runtime_error(msg); // Don't throw while debugging. Makes debugging with small amounts of data possible.
			#endif
		}
		Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> AtARegInv_EigenFullLU = luOfAtAReg.inverse();
		//Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> AtARegInv_Eigen = AtAReg_Eigen.inverse(); // This would be the cheap variant (PartialPivotLU), but we can't check if the matrix is invertible.
		Mat AtARegInvFullLU(AtARegInv_EigenFullLU.rows(), AtARegInv_EigenFullLU.cols(), CV_32FC1, AtARegInv_EigenFullLU.data()); // create an OpenCV Mat header for the Eigen data
		std::chrono::time_point<std::chrono::system_clock> inverseTimeEnd = std::chrono::system_clock::now();
		elapsed_mseconds = std::chrono::duration_cast<std::chrono::milliseconds>(inverseTimeEnd - inverseTimeStart).count();
		logger.debug("Inverting the regularized AtA took " + lexical_cast<string>(elapsed_mseconds)+"ms.");
		//Mat AtARegInvOCV = AtAReg.inv(); // slow OpenCV inv() for comparison

		// Todo(1): Moving AtA by lambda should move the eigenvalues by lambda, however, it does not. It did however on an early test (with rand maybe?).
		//Eigen::SelfAdjointEigenSolver<Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> es(AtAReg_Eigen);
		//std::cout << es.eigenvalues() << std::endl;

		Mat AtARegInvAt = AtARegInvFullLU * featureMatrix.t(); // Todo: We could use luOfAtAReg.solve(b, x) instead of .inverse() and these lines.
		Mat AtARegInvAtb = AtARegInvAt * deltaShape; // = x
		end = std::chrono::system_clock::now();
		elapsed_mseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
		logger.debug("Total time for solving the least-squares problem: " + lexical_cast<string>(elapsed_mseconds)+"ms.");

		Mat R = AtARegInvAtb;
		regressorData.push_back(R);

		// output (optional):
		for (auto currentImage = 0; currentImage < trainingImages.size(); ++currentImage) {
			Mat img = trainingImages[currentImage];
			Mat output = img.clone();
			for (int sample = 0; sample < numSamplesPerImage + 1; ++sample) {
				int currentRowInAllData = currentImage * (numSamplesPerImage + 1) + sample;
				// gt:
				for (int i = 0; i < numModelLandmarks; ++i) {
					cv::circle(output, cv::Point2f(groundtruthShapes.at<float>(currentRowInAllData, i), groundtruthShapes.at<float>(currentRowInAllData, i + numModelLandmarks)), 2, Scalar(255.0f, 0.0f, 0.0f));
				}
				// x_step: x at the position where we learn the features
				for (int i = 0; i < numModelLandmarks; ++i) {
					cv::circle(output, cv::Point2f(initialShape.at<float>(currentRowInAllData, i), initialShape.at<float>(currentRowInAllData, i + numModelLandmarks)), 2, Scalar(210.0f, 255.0f, 0.0f));
				}
				// could output x_new: The one after applying the learned R.
				Mat shapeStep = featureMatrix.row(currentRowInAllData) * R;
				Mat x_new = initialShape.row(currentRowInAllData) + shapeStep;
				for (int i = 0; i < numModelLandmarks; ++i) {
					cv::circle(output, cv::Point2f(x_new.at<float>(i), x_new.at<float>(i + numModelLandmarks)), 2, Scalar(255.0f, 185.0f, 0.0f));
				}
			}
		}

		// Prepare the data for the next step (and to output the error):
		Mat shapeStep = featureMatrix * R;
		initialShape = initialShape + shapeStep;
		deltaShape = groundtruthShapes - initialShape;
		// the error:
		double avgErr = cv::norm(deltaShape, cv::NORM_L1) / (deltaShape.rows * deltaShape.cols); // TODO: Doesn't say much, need to normalize by IED! But maybe not at training time, should work with all landmarks
		logger.debug("LM-SDM training: Average pixel error after applying all learned regressors: " + lexical_cast<string>(avgErr));
	}

	// Do the following:
	// * (later: GenericSupervisedDescentTraining? abstract base-class or not?)
	// - time measurement / output for mean calc etc
	// - update ML script for ZF's model
	// - more params, mean etc, my mean...
	// - draw curves

	// delete any unnecessary descriptorExtractors, in case the user selected a numCascadeSteps that's smaller than the number of provided descriptorExtractors.
	descriptorTypes.erase(begin(descriptorTypes) + numCascadeSteps, std::end(descriptorTypes));
	descriptorExtractors.erase(begin(descriptorExtractors) + numCascadeSteps, std::end(descriptorExtractors));

	SdmLandmarkModel model(modelMean, modelLandmarks, regressorData, descriptorExtractors, descriptorTypes);
	return model;
}

float LandmarkBasedSupervisedDescentTraining::calculateTranslationVariance(cv::Mat groundtruth, cv::Mat estimate, float normalization)
{
	// calculate the centroid of the ground-truth and the estimate
	Scalar gtMean = cv::mean(groundtruth);
	Scalar estMean = cv::mean(estimate);

	//cv::circle(img, Point2f(gtMeanX[0], gtMeanY[0]), 2, Scalar(0.0f, 0.0f, 255.0f)); // gt
	//cv::circle(img, Point2f(x0MeanX[0], x0MeanY[0]), 2, Scalar(0.0f, 255.0f, 255.0f)); // x0

	return (gtMean[0] - estMean[0]) / normalization;
}

float LandmarkBasedSupervisedDescentTraining::calculateScaleVariance(cv::Mat groundtruth, cv::Mat estimate)
{
	// calculate the scaling difference between the ground-truth and the estimate
	double gtMin, gtMax;
	cv::minMaxIdx(groundtruth, &gtMin, &gtMax);
	double x0Min, x0Max;
	cv::minMaxIdx(estimate, &x0Min, &x0Max);

	//cv::rectangle(img, cv::Rect(minWidth, minHeight, maxWidth - minWidth, maxHeight - minHeight), Scalar(255.0f, 0.0f, 0.0f));

	return (gtMax - gtMin) / (x0Max - x0Min);
}

}
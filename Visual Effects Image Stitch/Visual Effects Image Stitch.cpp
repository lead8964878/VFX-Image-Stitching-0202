#include <iostream>
#include <stdio.h>
#include <vector>
#include <string>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <ctime>
#include <fstream>
#include <unordered_set>

using namespace cv;
using namespace std;

struct Features
{
	int reliability;
	vector<Vec2f> locations;
	vector<vector<int>> descriptons;
};

struct ResponseCoords
{
	double r;
	double c;
	int x;
	int y;
	ResponseCoords(double _r, double _c,int _x, int _y) :r(_r),c(_c), x(_x), y(_y) {}
};

struct InlierVotes
{
	int matchesIndex;
	int votes;
	InlierVotes(int _m,int _v):matchesIndex(_m),votes(_v){}
};

struct Scores
{
	int index;
	int score;
	Scores(int _i, int _s) :index(_i), score(_s) {}
};

float globalFocalLength = 0;
string outputFileName;

void LoadImages(string infoList, vector<Mat>& oriImages)
{
	fstream infoListFile(infoList);
	string fileName;
	infoListFile >> outputFileName;
	infoListFile >> globalFocalLength;
	while (infoListFile >> fileName)
	{
		Mat image = imread(fileName);
		oriImages.push_back(image);
	}
	infoListFile.close();
}

void GetImageGradients(Mat &src, Mat &Ix, Mat &Iy)
{
	Mat kernelX(1, 3, CV_64F);
	kernelX.at<double>(0, 0) = -1.0f;
	kernelX.at<double>(0, 1) = 0.0f;
	kernelX.at<double>(0, 2) = 1.0f;

	filter2D(src, Ix, CV_64F, kernelX);

	Mat kernelY(3, 1, CV_64F);
	kernelY.at<double>(0, 0) = -1.0f;
	kernelY.at<double>(1, 0) = 0.0f;
	kernelY.at<double>(2, 0) = 1.0f;

	filter2D(src, Iy, CV_64F, kernelY);
}

void FeatureDescripton(vector<Vec2f>& locations, Mat &Io, vector<vector<int>>& descriptons)
{
	//cout << locations.size() << endl;
	for (int i = 0; i < locations.size(); i++)
	{
		int y = locations[i][0];
		int x = locations[i][1];
		vector<int> votes;
		votes.resize(128);
		for (int v = 0; v < votes.size(); v++)votes[v] = 0;

		int blockStart[4] = { -8, -4, 1, 5 };
		for (int by = 0; by < 4; by++)
		{
			int y_ = y + blockStart[by];
			for (int bx = 0; bx < 4; bx++)
			{
				int x_ = x + blockStart[bx];
				for (int dy = 0; dy < 4; dy++)
				{
					for (int dx = 0; dx < 4; dx++)
					{
						int idx = 8 * (4 * by + bx) + floor(Io.at<double>(y_ + dy, x_ + dx));
						votes[idx]++;
					}
				}
			}
		}
		descriptons.push_back(votes);
	}
}

bool RCompare(ResponseCoords rc1, ResponseCoords rc2){return rc1.r > rc2.r;}


void CalculateFeatures(Mat image, Features& f)
{
	Mat gray;
	cvtColor(image, gray, COLOR_RGB2GRAY);

	Mat Ix, Iy;
	GetImageGradients(gray, Ix, Iy);

	Mat A, B, C;
	GaussianBlur(Ix.mul(Ix), A, Size(5, 5), 1);
	GaussianBlur(Iy.mul(Iy), B, Size(5, 5), 1);
	GaussianBlur(Ix.mul(Iy), C, Size(5, 5), 1);

	Mat R(gray.size(), CV_64F);

	for (int i = 0; i < image.rows; i++)
	{
		for (int j = 0; j < image.cols; j++)
		{
			double k = 0.04;
			double vA = A.at<double>(i, j);
			double vB = B.at<double>(i, j);
			double vC = C.at<double>(i, j);
			R.at<double>(i, j) = vA * vB - vC * vC - k * pow(vA + vB, 2);
		}
	}

	vector<ResponseCoords> keypointList;
	for (int i = 9; i < image.rows - 9; i++)
	{
		for (int j = 9; j < image.cols - 9; j++)
		{

			if (R.at<double>(i, j) > R.at<double>(i - 1, j) &&
				R.at<double>(i, j) > R.at<double>(i + 1, j) &&
				R.at<double>(i, j) > R.at<double>(i, j - 1) &&
				R.at<double>(i, j) > R.at<double>(i, j + 1) &&
				R.at<double>(i, j) > 20000)
			{
				ResponseCoords rc(10000000,R.at<double>(i, j), i, j);
				keypointList.push_back(rc);
			}
		}
	}

	for (int i = 0; i < keypointList.size(); i++)
	{
		int ED = 1000000;
		for (int j = 0; j < keypointList.size(); j++)
		{
			if (keypointList[j].c > keypointList[i].c)
				ED = pow(keypointList[j].x - keypointList[i].x, 2) + pow(keypointList[j].y - keypointList[i].y, 2);
			if (keypointList[i].r > ED)
				keypointList[i].r = ED;
		}
	}
	sort(keypointList.begin(), keypointList.end(), RCompare);
	for (int i = 0; i < 500; i++)
	{
		Vec2f v(keypointList[i].x, keypointList[i].y);
		f.locations.push_back(v);
	}


	Mat Io(Ix.size(), CV_64F);
	for (int i = 0; i < Ix.rows; i++)
		for (int j = 0; j < Ix.cols; j++)
			Io.at<double>(i, j) = fastAtan2(Ix.at<double>(i, j), Iy.at<double>(i, j)) / 45.0f;

	FeatureDescripton(f.locations, Io, f.descriptons);

}

void FeatureMatching(Features& f1, Features &f2, vector<Vec2i> &matches)
{
	//float SCORE_RATE = 18.f;
	//for (int i = 0; i < f1.locations.size(); i++)
	//{
	//	vector<int> distance;
	//	distance.resize(f2.locations.size());
	//	distance.clear();
	//	double minScore = 2147483647;
	//	int maxIndex = -1;
	//	for (int j = 0; j < f2.locations.size(); j++)
	//	{
	//		double sum = 0;
	//		for (int k = 0; k < 128; k++)
	//		{
	//			sum += pow(f1.descriptons[i][k] - f2.descriptons[j][k],2);
	//		}
	//		double score = sqrt(sum);

	//		if (score < minScore)
	//		{
	//			minScore = score;
	//			maxIndex = j;
	//		}
	//	}
	//	cout << minScore << endl;
	//	if (minScore < SCORE_RATE)
	//	{
	//		Vec2i match(i, maxIndex);
	//		matches.push_back(match);
	//	}
	//}
	//cout << "Matches: " << matches.size() << endl;
	float SCORE_RATE = 0.813f;
	for (int i = 0; i < f1.locations.size(); i++)
	{
		vector<int> distance;
		distance.resize(f2.locations.size());
		distance.clear();
		double maxScore = -2147483647;
		int maxIndex = -1;
		for (int j = 0; j < f2.locations.size(); j++)
		{
			//Cosine Similarity
			double sum = 0, len1 = 0, len2 = 0;
			for (int k = 0; k < 128; k++)
			{
				sum += f1.descriptons[i][k] * f2.descriptons[j][k];
				len1 += pow(f1.descriptons[i][k],2);
				len2 += pow(f2.descriptons[j][k],2);
			}
			double score = sum / (sqrt(len1)*sqrt(len2));

			if (score > maxScore)
			{
				maxScore = score;
				maxIndex = j;
			}
		}
		//cout << maxScore << endl;
		if (maxScore > SCORE_RATE)
		{
			Vec2i match(i,maxIndex);
			matches.push_back(match);
		}
	}
	cout << "Matches: " << matches.size() << endl;
}

bool SCompare(Scores s1, Scores s2) { return s1.score < s2.score; }

void RemoveOutliers(int offset, Features &f1, Features &f2, vector<Vec2i> &matches, vector<Vec2i> &cleanedMatches)
{
	vector<Scores> scores;
	vector< int> dX;
	vector< int> dY;
	for (int i = 0; i < matches.size(); i++)
	{
		int x1 = f1.locations[matches[i][0]][1] + offset;
		int y1 = f1.locations[matches[i][0]][1];
		int x2 = f2.locations[matches[i][1]][1];
		int y2 = f2.locations[matches[i][1]][1];
		dX.push_back(x1 - x2);
		dY.push_back(y1 - y2);
	}

	for (int i = 0; i < matches.size(); i++)
	{
		int scoreTmp = 0;
		for (int j = 0; j < matches.size(); j++)
		{
			scoreTmp = sqrt(pow(abs(dX[i] - dX[j]), 2) + pow(abs(dY[i] - dY[j]), 2));
		}
		Scores sc(i, scoreTmp);
		scores.push_back(sc);
	}

	sort(scores.begin(), scores.end(),SCompare);

	for (int i = 0; i < matches.size()*0.3f; i++)
	{
		cleanedMatches.push_back(matches[scores[i].index]);
	}
	cout << "Cleaned Matches: " << cleanedMatches.size() << endl;
}

/* Another Method of RemoveOutlier */
/*
bool ICompare(InlierVotes iv1, InlierVotes iv2) { return iv1.votes > iv2.votes; }

void RANSAC(Features &f1, Features &f2, vector<Vec2i> &matches, Vec2i cleanedMatches)
{
	float ACCEPT_DISTANCE = 100.f;

	vector<InlierVotes> reliability;
	cout << "Matches Size:" << matches.size() << endl;
	for (int i = 0; i < matches.size(); i++)
	{
		int inlierVotes=0;
		int dx = f2.locations[matches[i][1]][0] - f1.locations[matches[i][0]][0];
		int dy = f2.locations[matches[i][1]][1] - f1.locations[matches[i][0]][1];

		for (int j = 0; j < matches.size(); j++)
		{
			if (j != i)
			{
				int p1x = f1.locations[matches[j][0]][0] + dx;
				int p2x = f2.locations[matches[j][1]][0];
				int p1y = f1.locations[matches[j][0]][1] + dy;
				int p2y = f2.locations[matches[j][1]][1];
				float distance = pow(p1x - p2x, 2) + pow(p1y - p2y, 2);
				
				if (distance < ACCEPT_DISTANCE)
				{
					inlierVotes++;
				}
			}
		}
		InlierVotes v(i,inlierVotes);
		reliability.push_back(v);
	}

	sort(reliability.begin(), reliability.end(), ICompare);

	for (int i = 0; i < reliability.size(); i++)
	{
		cout << (float)reliability[i].votes / (float)matches.size()<<endl;
	}
}*/

void Combine2Images(Mat &src1, Mat &src2, Mat &dst)
{
	Mat M(max(src1.rows, src2.rows), src1.cols + src2.cols, CV_8UC3, Scalar::all(0));

	Mat left(M, Rect(0, 0, src1.cols, src1.rows));
	src1.copyTo(left);

	Mat right(M, Rect(src1.cols, 0, src2.cols, src2.rows));
	src2.copyTo(right);

	dst = M;
}
void FeatureComposite(vector<Mat>& oriImages, vector<Features>& featureList)
{
	Mat displayImg1,displayImg2;

	for (int i = 0; i < oriImages.size()-1; i++) {

		cout << "#" << i + 1 << " Composition" << endl;
		oriImages[i].copyTo(displayImg1);
		oriImages[i + 1].copyTo(displayImg2);


		for (int j = 0; j < featureList[i].locations.size(); j++)
		{
			int x = featureList[i].locations[j][0];
			int y = featureList[i].locations[j][1];
			rectangle(displayImg1, Point(y + 2, x + 2), Point(y - 2, x - 2), Scalar((0, 0, 255)));
		}

		for (int j = 0; j < featureList[i + 1].locations.size(); j++)
		{
			int x = featureList[i + 1].locations[j][0];
			int y = featureList[i + 1].locations[j][1];
			rectangle(displayImg2, Point(y + 2, x + 2), Point(y - 2, x - 2), Scalar((0, 0, 255)));
		}
		//namedWindow("1");
		//imshow("1", displayImg1);
		//namedWindow("2");
		//imshow("2", displayImg2);

		cout << "Matching Features" << endl;

		vector<Vec2i> matches;
		FeatureMatching(featureList[i], featureList[i + 1], matches);

		cout << "Detecting Outliers" << endl;

		//Vec2i cleanedMatches;
		//RANSAC(featureList[i], featureList[i + 1], matches, cleanedMatches);
		vector<Vec2i> cleanedMatches;
		RemoveOutliers(displayImg1.cols, featureList[i], featureList[i + 1], matches, cleanedMatches);
		matches = cleanedMatches;

		cv::Mat matchImage;
		Combine2Images(displayImg1, displayImg2, matchImage);
		for (int j = 0; j < matches.size(); j++)
		{
			int x1 = featureList[i].locations[matches[j][0]][1];
			int y1 = featureList[i].locations[matches[j][0]][0];
			int x2 = featureList[i+1].locations[matches[j][1]][1] + displayImg1.cols;
			int y2 = featureList[i+1].locations[matches[j][1]][0];
			line(matchImage, Point(x1, y1), Point(x2, y2), Scalar(rand() % 256, rand() % 256, rand() % 256));
		}
		namedWindow("combined");
		imshow("combined", matchImage);
		 //imwrite(outputFileName, matchImage);
		waitKey(0);
	}
}

int main()
{
	vector<Mat> oriImages;
	LoadImages("D:\\Course_project\\Visual Effects Image Stitch\\denny\\InfoList.txt",oriImages);

	cout << "Calculating Features" << endl;
	vector<Features> featureList;
	for (int i = 0; i < oriImages.size(); i++)
	{
		cout << "Image" << i << endl;
		Features features;
		CalculateFeatures(oriImages[i], features);
		featureList.push_back(features);
	}

	cout << "Compositing Features" << endl;
	FeatureComposite(oriImages, featureList);

	cout << "Hello World!\n";
	system("pause");
}

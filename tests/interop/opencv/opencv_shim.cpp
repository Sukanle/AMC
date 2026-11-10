// Minimal C ABI over OpenCV's C++ API. OpenCV 5 removed the legacy C API, so
// the Rust bridge talks to this shim instead of cv* C functions.

#include <opencv2/core.hpp>

extern "C" {

void *opencv_shim_mat_new(int rows, int cols) {
    try {
        return new cv::Mat(rows, cols, CV_64F, cv::Scalar(0.0));
    } catch (...) {
        return nullptr;
    }
}

void opencv_shim_mat_destroy(void *mat) { delete static_cast<cv::Mat *>(mat); }

int opencv_shim_mat_set(void *mat, int row, int col, double value) {
    auto *matrix = static_cast<cv::Mat *>(mat);
    if (matrix == nullptr || row < 0 || col < 0 || row >= matrix->rows || col >= matrix->cols) {
        return -1;
    }
    try {
        matrix->at<double>(row, col) = value;
        return 0;
    } catch (...) {
        return -2;
    }
}

double opencv_shim_mat_get(const void *mat, int row, int col) {
    const auto *matrix = static_cast<const cv::Mat *>(mat);
    if (matrix == nullptr || row < 0 || col < 0 || row >= matrix->rows || col >= matrix->cols) {
        return 0.0;
    }
    try {
        return matrix->at<double>(row, col);
    } catch (...) {
        return 0.0;
    }
}

double opencv_shim_mat_sum(const void *mat) {
    const auto *matrix = static_cast<const cv::Mat *>(mat);
    if (matrix == nullptr) {
        return 0.0;
    }
    try {
        return cv::sum(*matrix)[0];
    } catch (...) {
        return 0.0;
    }
}

}   // extern "C"

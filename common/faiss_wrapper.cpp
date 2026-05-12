#include "../include/faiss_wrapper.h"
#include <cmath>
#include <algorithm>
#include <fstream>

FaissIndex::FaissIndex() : dimension_(0), initialized_(false) {}

FaissIndex::~FaissIndex() {}

bool FaissIndex::init(int dimension, const std::string &index_path) {
    if (dimension <= 0) return false;
    dimension_ = dimension;
    index_path_ = index_path;
    initialized_ = true;
    vectors_.clear();
    ids_.clear();
    return true;
}

bool FaissIndex::add_vector(const float *vector, int id) {
    if (!initialized_ || vector == nullptr) return false;
    
    for (int i = 0; i < dimension_; i++) {
        vectors_.push_back(vector[i]);
    }
    ids_.push_back(id);
    return true;
}

static float cosine_similarity(const float *a, const float *b, int dim) {
    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
    for (int i = 0; i < dim; i++) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
    if (norm_a == 0 || norm_b == 0) return 0;
    return dot / (sqrt(norm_a) * sqrt(norm_b));
}

std::vector<std::pair<int, float>> FaissIndex::search(const float *query_vector, int top_k, float threshold) {
    std::vector<std::pair<int, float>> results;
    
    if (!initialized_ || vectors_.empty()) return results;
    
    int n = ids_.size();
    std::vector<std::pair<float, int>> scores;
    
    for (int i = 0; i < n; i++) {
        float *vec = &vectors_[i * dimension_];
        float sim = cosine_similarity(query_vector, vec, dimension_);
        if (sim >= threshold) {
            scores.push_back({sim, ids_[i]});
        }
    }
    
    std::sort(scores.begin(), scores.end(), 
              [](const std::pair<float, int> &a, const std::pair<float, int> &b) {
                  return a.first > b.first;
              });
    
    for (int i = 0; i < std::min(top_k, (int)scores.size()); i++) {
        results.push_back({scores[i].second, scores[i].first});
    }
    
    return results;
}

bool FaissIndex::save(const std::string &index_path) {
    if (!initialized_) return false;
    
    std::ofstream ofs(index_path, std::ios::binary);
    if (!ofs) return false;
    
    ofs.write(reinterpret_cast<char*>(&dimension_), sizeof(int));
    
    int n = ids_.size();
    ofs.write(reinterpret_cast<char*>(&n), sizeof(int));
    
    ofs.write(reinterpret_cast<char*>(vectors_.data()), vectors_.size() * sizeof(float));
    ofs.write(reinterpret_cast<char*>(ids_.data()), ids_.size() * sizeof(int));
    
    ofs.close();
    return true;
}

bool FaissIndex::load(const std::string &index_path) {
    std::ifstream ifs(index_path, std::ios::binary);
    if (!ifs) return false;
    
    ifs.read(reinterpret_cast<char*>(&dimension_), sizeof(int));
    int n;
    ifs.read(reinterpret_cast<char*>(&n), sizeof(int));
    
    vectors_.resize(n * dimension_);
    ids_.resize(n);
    
    ifs.read(reinterpret_cast<char*>(vectors_.data()), vectors_.size() * sizeof(float));
    ifs.read(reinterpret_cast<char*>(ids_.data()), ids_.size() * sizeof(int));
    
    ifs.close();
    initialized_ = true;
    index_path_ = index_path;
    return true;
}

void FaissIndex::reset() {
    vectors_.clear();
    ids_.clear();
    initialized_ = false;
}

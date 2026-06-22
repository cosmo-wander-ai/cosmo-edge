#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cosmo::db {

// ---------------------------------------------------------------------------
// Common library info — shared shape for face/body/things libraries
// ---------------------------------------------------------------------------

struct LibInfo {
    std::string id;
    std::string name;
    int type{-1};
    double threshold{0.0};
    int max_capacity{0};
    int stranger_alarm{0};
    float stranger_threshold{0.0};
};

// ---------------------------------------------------------------------------
// PersonDao (face library) types
// ---------------------------------------------------------------------------

// Query condition for face library list
struct FaceLibQueryCondition {
    std::string camera_id;
    int task_type{2};
    std::string face_lib_name;
    std::string face_lib_id;
    int page_num{1};
    int page_size{10};
};

// Single face library record in query result
struct FaceLibRecord {
    std::string id;
    std::string name;
    int type{-1};
    double threshold{0.0};
    int max_face_number{0};
    int stranger_alarm{0};
    float stranger_threshold{0.0};
    int person_number{0};
    int face_number{0};
    int64_t create_timestamp{0};
    int64_t update_timestamp{0};
};

// Face library list query result
struct FaceLibQueryResult {
    int search_all{0};
    int face_lib_count{0};
    std::vector<FaceLibRecord> face_lib_list;
};

// Query condition for persons in face libraries
struct FacePersonQueryCondition {
    std::vector<std::string> face_lib_id_list;
    std::string person_id;
    std::string person_name;
    std::string serial_number;
    int page_num{1};
    int page_size{10};
    std::string query_id;
};

// Picture info in face person query result
struct FacePersonPicture {
    std::string id;
    std::string url;
};

// Face library reference in person result
struct FaceLibRef {
    std::string id;
    std::string name;
};

// Single person record in face query result
struct FacePersonRecord {
    std::string id;
    std::string name;
    int64_t create_timestamp{0};
    int64_t update_timestamp{0};
    std::vector<FaceLibRef> face_lib_list;
    std::vector<FacePersonPicture> picture_list;
    std::string serial_number;
};

// Face person query result
struct FacePersonQueryResult {
    std::string query_id;
    int total_count{0};
    std::vector<FacePersonRecord> person_list;
};

// Person add/update condition (replaces MsgConditionLib)
struct PersonCondition {
    int person_operation{1};
    std::vector<std::string> face_lib_id;
    std::string person_id;
    std::string person_name;
    std::vector<std::string> retain_picture_id;
    std::string serial_number;
    int64_t create_time{0};
    int64_t update_time{0};
};

// Face registration record types (used by flow/face/ and PersonDao)
struct FacePicInfo {
    std::string id;
    std::vector<float> feature;
};

struct FaceRegRecordUnit {
    int64_t face_update_time;
    int64_t face_create_time;
    std::string id;
    std::string face_name;
    std::string serial_name;
    std::vector<std::string> face_lib_id;
    std::vector<FacePicInfo> face_pic_infos;
};

// ---------------------------------------------------------------------------
// PersonRecogDao (body library) types
// ---------------------------------------------------------------------------

// Query condition for person recog library list
struct PersonRecogLibQueryCondition {
    std::string camera_id;
    int task_type{58};
    int person_lib_type{-1};
    std::string person_lib_name;
    std::string person_lib_id;
    int page_num{1};
    int page_size{10};
};

// Single person recog library record in query result
struct PersonRecogLibRecord {
    std::string id;
    std::string name;
    int type{-1};
    double threshold{0.0};
    int max_person_number{0};
    int stranger_alarm{0};
    float stranger_threshold{0.0};
    int person_number{0};
    int64_t create_timestamp{0};
    int64_t update_timestamp{0};
};

// Person recog library list query result
struct PersonRecogLibQueryResult {
    int search_all{0};
    int person_lib_count{0};
    std::vector<PersonRecogLibRecord> person_lib_list;
};

// Query condition for persons in recog libraries
struct PersonRecogQueryCondition {
    std::vector<std::string> person_lib_id_list;
    int person_lib_type{-1};
    std::string person_id;
    std::string picture_name;
    int page_num{1};
    int page_size{10};
    std::string query_id;
};

// Person recog library reference
struct PersonRecogLibRef {
    std::string id;
    std::string name;
};

// Single person record in recog query result
struct PersonRecogRecord {
    std::string id;
    std::string picture_name;
    std::string picture_url;
    int64_t create_timestamp{0};
    int64_t update_timestamp{0};
    std::vector<float> feature;
    PersonRecogLibRef person_lib;
};

// Person recog query result
struct PersonRecogQueryResult {
    std::string query_id;
    int total_count{0};
    std::vector<PersonRecogRecord> person_list;
};

// ---------------------------------------------------------------------------
// ArticlesReidDao (things library) types
// ---------------------------------------------------------------------------

// Query condition for things library list
struct ThingsLibQueryCondition {
    std::string camera_id;
    int task_type{148};
    int things_lib_type{-1};
    std::string things_lib_name;
    std::string things_lib_id;
    int page_num{1};
    int page_size{10};
};

// Single things library record in query result
struct ThingsLibRecord {
    std::string id;
    std::string name;
    int type{-1};
    double threshold{0.0};
    int max_things_number{0};
    int stranger_alarm{0};
    float stranger_threshold{0.0};
    int things_number{0};
    int64_t create_timestamp{0};
    int64_t update_timestamp{0};
};

// Things library list query result
struct ThingsLibQueryResult {
    int search_all{0};
    int things_lib_count{0};
    std::vector<ThingsLibRecord> things_lib_list;
};

// Query condition for articles in things libraries
struct ThingsQueryCondition {
    std::vector<std::string> things_lib_id_list;
    int things_lib_type{-1};
    std::string things_id;
    std::string picture_name;
    int page_num{1};
    int page_size{10};
    std::string query_id;
};

// Things library reference
struct ThingsLibRef {
    std::string id;
    std::string name;
};

// Single article record in things query result
struct ThingsRecord {
    std::string id;
    std::string picture_name;
    std::string picture_url;
    int64_t create_timestamp{0};
    int64_t update_timestamp{0};
    std::vector<float> feature;
    ThingsLibRef things_lib;
};

// Things query result
struct ThingsQueryResult {
    std::string query_id;
    int total_count{0};
    std::vector<ThingsRecord> things_list;
};

}  // namespace cosmo::db

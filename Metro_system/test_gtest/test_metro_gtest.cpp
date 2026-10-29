#include <gtest/gtest.h>

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>
#include <iterator>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif

extern "C" {
#include "common.h"
#include "station.h"
#include "bst.h"
#include "storage.h"
#include "auth.h"
#include "audit_log.h"
#include "station_validation_threads.h"
}

namespace fs = std::filesystem;

extern "C" int metro_app_main(void);

namespace {
fs::path originalCwd;
fs::path testRoot;

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << "Could not open fixture: " << path.string();
    out << text;
    out.close();
    ASSERT_TRUE(out.good()) << "Could not write fixture: " << path.string();
}

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

Station makeStation(int id, const char* name,
                    float distance = 1.5f,
                    float fare = 10.0f,
                    int platform = 1) {
    Station s{};
    s.id = id;
    std::snprintf(s.name, sizeof(s.name), "%s", name);
    s.distance = distance;
    s.fare = fare;
    s.platform = platform;
    return s;
}

void writeDefaultStations() {
    writeText("data/stations.csv",
        "id,name,distance,fare,platform\n"
        "101,Central,1.0,10.0,1\n"
        "102,Park,2.0,12.0,2\n"
        "103,Airport,3.0,15.0,3\n"
        "104,Museum,4.0,18.0,1\n"
        "105,Harbour,5.0,20.0,2\n"
        "106,University,6.0,22.0,3\n");
}

void writeDefaultUsers() {
    writeText("data/users.csv",
        "username,password,role\n"
        "admin,admin123,ADMIN\n"
        "viewer,viewer123,VIEWER\n");
}

class TestEnvironment final : public ::testing::Environment {
public:
    void SetUp() override {
        originalCwd = fs::current_path();
        testRoot = fs::temp_directory_path() / "metro_management_gtest";
        std::error_code ec;
        fs::remove_all(testRoot, ec);
        fs::create_directories(testRoot / "data");
        fs::current_path(testRoot);
        writeDefaultStations();
        writeDefaultUsers();
        writeText("data/audit.log", "");
    }

    void TearDown() override {
        fs::current_path(originalCwd);
        std::error_code ec;
        fs::remove_all(testRoot, ec);
    }
};

int verifyAvlNode(const StationNode* node, long long lower, long long upper,
                  bool* valid) {
    if (node == nullptr) return 0;
    if (node->station.id <= lower || node->station.id >= upper) *valid = false;
    int leftHeight = verifyAvlNode(node->left, lower, node->station.id, valid);
    int rightHeight = verifyAvlNode(node->right, node->station.id, upper, valid);
    int expectedHeight = 1 + (leftHeight > rightHeight ? leftHeight : rightHeight);
    if (node->height != expectedHeight || leftHeight - rightHeight > 1 ||
        rightHeight - leftHeight > 1) {
        *valid = false;
    }
    return expectedHeight;
}

bool avlInvariantsHold(const StationNode* root) {
    bool valid = true;
    (void)verifyAvlNode(root, std::numeric_limits<long long>::min(),
                        std::numeric_limits<long long>::max(), &valid);
    return valid;
}

class AvlTest : public ::testing::Test {
protected:
    StationNode* root = nullptr;
    void TearDown() override { freeStationTree(root); }
};

TEST(AvlPublicApi, CreateNodeInitializesFieldsAndCanBeFreed) {
    Station s = makeStation(42, "Test");
    StationNode* node = createNode(s);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->station.id, 42);
    EXPECT_EQ(node->height, 1);
    EXPECT_EQ(node->left, nullptr);
    EXPECT_EQ(node->right, nullptr);
    freeStationTree(node);
}

TEST_F(AvlTest, InsertsAndBalancesAllFourRotationPatterns) {
    const int cases[4][3] = {{30,20,10}, {10,20,30}, {30,10,20}, {10,30,20}};
    for (const auto& ids : cases) {
        StationNode* local = nullptr;
        for (int id : ids) {
            ASSERT_EQ(insertStation(&local, makeStation(id, "Node")), APP_SUCCESS);
        }
        ASSERT_NE(local, nullptr);
        EXPECT_EQ(local->station.id, 20);
        EXPECT_EQ(local->height, 2);
        EXPECT_NE(searchStation(local, 10), nullptr);
        EXPECT_NE(searchStation(local, 30), nullptr);
        freeStationTree(local);
    }
}

TEST_F(AvlTest, RejectsInvalidInputsAndDuplicateIds) {
    EXPECT_EQ(insertStation(nullptr, makeStation(1, "One")), APP_INVALID_INPUT);
    EXPECT_EQ(insertStation(&root, makeStation(0, "Zero")), APP_INVALID_INPUT);
    EXPECT_EQ(insertStation(&root, makeStation(1, "")), APP_INVALID_INPUT);
    EXPECT_EQ(insertStation(&root, makeStation(1, "Negative", -1.0f)), APP_INVALID_INPUT);
    EXPECT_EQ(insertStation(&root, makeStation(1, "NaN", std::numeric_limits<float>::quiet_NaN())), APP_INVALID_INPUT);
    EXPECT_EQ(insertStation(&root, makeStation(1, "Inf", std::numeric_limits<float>::infinity())), APP_INVALID_INPUT);
    EXPECT_EQ(insertStation(&root, makeStation(1, "Fare", 1.0f, -1.0f)), APP_INVALID_INPUT);
    EXPECT_EQ(insertStation(&root, makeStation(1, "FareNaN", 1.0f, std::numeric_limits<float>::quiet_NaN())), APP_INVALID_INPUT);
    EXPECT_EQ(insertStation(&root, makeStation(1, "Platform", 1.0f, 1.0f, 0)), APP_INVALID_INPUT);
    Station unterminated{};
    unterminated.id = 1;
    std::memset(unterminated.name, 'X', sizeof(unterminated.name));
    unterminated.distance = 1.0f;
    unterminated.fare = 1.0f;
    unterminated.platform = 1;
    EXPECT_EQ(insertStation(&root, unterminated), APP_INVALID_INPUT);
    EXPECT_EQ(insertStation(&root, makeStation(10, "Ten")), APP_SUCCESS);
    EXPECT_EQ(insertStation(&root, makeStation(10, "Duplicate")), APP_DUPLICATE);
    EXPECT_EQ(countStations(root), 1);
}

TEST_F(AvlTest, SearchCountAndMinimumMaximumHandleEmptyAndPopulatedTrees) {
    EXPECT_EQ(countStations(nullptr), 0);
    EXPECT_EQ(searchStation(nullptr, 1), nullptr);
    int id = -99;
    EXPECT_EQ(findMinimumStationId(nullptr, &id), 0);
    EXPECT_EQ(findMaximumStationId(nullptr, &id), 0);
    EXPECT_EQ(findMinimumStationId(nullptr, nullptr), 0);
    EXPECT_EQ(findMaximumStationId(nullptr, nullptr), 0);

    ASSERT_EQ(insertStation(&root, makeStation(20, "Twenty")), APP_SUCCESS);
    ASSERT_EQ(insertStation(&root, makeStation(10, "Ten")), APP_SUCCESS);
    ASSERT_EQ(insertStation(&root, makeStation(30, "Thirty")), APP_SUCCESS);
    EXPECT_EQ(countStations(root), 3);
    EXPECT_EQ(searchStation(root, 10)->station.id, 10);
    EXPECT_EQ(searchStation(root, 30)->station.id, 30);
    EXPECT_EQ(searchStation(root, 99), nullptr);
    ASSERT_EQ(findMinimumStationId(root, &id), 1);
    EXPECT_EQ(id, 10);
    ASSERT_EQ(findMaximumStationId(root, &id), 1);
    EXPECT_EQ(id, 30);
    EXPECT_EQ(findMinimumStationId(root, nullptr), 0);
    EXPECT_EQ(findMaximumStationId(root, nullptr), 0);
}

TEST_F(AvlTest, UpdateValidatesIdDataAndMissingStations) {
    Station updated = makeStation(5, "Updated", 9.0f, 25.0f, 4);
    EXPECT_EQ(updateStation(nullptr, 5, updated), APP_NOT_FOUND);
    EXPECT_EQ(updateStation(root, 0, updated), APP_INVALID_INPUT);
    EXPECT_EQ(updateStation(root, 5, makeStation(6, "Mismatch")), APP_INVALID_INPUT);
    EXPECT_EQ(updateStation(root, 5, makeStation(5, "")), APP_INVALID_INPUT);
    EXPECT_EQ(updateStation(root, 5, makeStation(5, "NegativeDistance", -1.0f)), APP_INVALID_INPUT);
    EXPECT_EQ(updateStation(root, 5, makeStation(5, "NaNDistance",
                  std::numeric_limits<float>::quiet_NaN())), APP_INVALID_INPUT);
    EXPECT_EQ(updateStation(root, 5, makeStation(5, "InfiniteDistance",
                  std::numeric_limits<float>::infinity())), APP_INVALID_INPUT);
    EXPECT_EQ(updateStation(root, 5, makeStation(5, "NegativeFare", 1.0f, -1.0f)), APP_INVALID_INPUT);
    EXPECT_EQ(updateStation(root, 5, makeStation(5, "InfiniteFare", 1.0f,
                  std::numeric_limits<float>::infinity())), APP_INVALID_INPUT);
    EXPECT_EQ(updateStation(root, 5, makeStation(5, "NaNFare", 1.0f,
                  std::numeric_limits<float>::quiet_NaN())), APP_INVALID_INPUT);
    EXPECT_EQ(updateStation(root, 5, makeStation(5, "BadPlatform", 1.0f, 1.0f, 0)), APP_INVALID_INPUT);
    ASSERT_EQ(insertStation(&root, makeStation(5, "Before")), APP_SUCCESS);
    EXPECT_EQ(updateStation(root, 5, updated), APP_SUCCESS);
    ASSERT_NE(searchStation(root, 5), nullptr);
    EXPECT_STREQ(searchStation(root, 5)->station.name, "Updated");
    EXPECT_FLOAT_EQ(searchStation(root, 5)->station.fare, 25.0f);
    EXPECT_EQ(updateStation(root, 9, makeStation(9, "Missing")), APP_NOT_FOUND);
}

TEST_F(AvlTest, DeletesLeafOneChildTwoChildrenAndMissingKeys) {
    EXPECT_EQ(deleteStation(nullptr, 1), APP_INVALID_INPUT);
    EXPECT_EQ(deleteStation(&root, 0), APP_INVALID_INPUT);
    EXPECT_EQ(deleteStation(&root, 1), APP_NOT_FOUND);

    for (int id : {40, 20, 60, 10, 30, 50, 70, 25}) {
        ASSERT_EQ(insertStation(&root, makeStation(id, "Node")), APP_SUCCESS);
    }
    EXPECT_EQ(deleteStation(&root, 999), APP_NOT_FOUND);
    EXPECT_EQ(countStations(root), 8);
    EXPECT_EQ(deleteStation(&root, 10), APP_SUCCESS); // leaf
    EXPECT_EQ(deleteStation(&root, 30), APP_SUCCESS); // node with one child (25)
    EXPECT_EQ(deleteStation(&root, 40), APP_SUCCESS); // node with two children
    EXPECT_EQ(searchStation(root, 40), nullptr);
    EXPECT_EQ(countStations(root), 5);
}

TEST_F(AvlTest, DeletionKeepsAvlBalanceAndStoredHeightsValid) {
    // A larger deterministic tree exercises repeated rebalancing as leaves,
    // one-child nodes, and two-child nodes are removed.
    for (int id = 1; id <= 31; ++id) {
        ASSERT_EQ(insertStation(&root, makeStation(id, "Node")), APP_SUCCESS);
        EXPECT_TRUE(avlInvariantsHold(root)) << "after inserting " << id;
    }

    const int deletionOrder[] = {
        1, 31, 16, 8, 24, 4, 12, 20, 28, 2, 6, 10, 14, 18, 22, 26,
        30, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29
    };
    for (int id : deletionOrder) {
        ASSERT_EQ(deleteStation(&root, id), APP_SUCCESS) << "deleting " << id;
        EXPECT_TRUE(avlInvariantsHold(root)) << "after deleting " << id;
    }
    EXPECT_EQ(root, nullptr);
    EXPECT_EQ(countStations(root), 0);
}

TEST_F(AvlTest, UpdatePreservesTreeOrderingAndOtherNodes) {
    ASSERT_EQ(insertStation(&root, makeStation(20, "Twenty")), APP_SUCCESS);
    ASSERT_EQ(insertStation(&root, makeStation(10, "Ten")), APP_SUCCESS);
    ASSERT_EQ(insertStation(&root, makeStation(30, "Thirty")), APP_SUCCESS);

    Station replacement = makeStation(20, "TwentyUpdated", 2.0f, 3.0f, 2);
    EXPECT_EQ(updateStation(root, 20, replacement), APP_SUCCESS);
    EXPECT_NE(searchStation(root, 10), nullptr);
    EXPECT_NE(searchStation(root, 30), nullptr);
    EXPECT_STREQ(searchStation(root, 20)->station.name, "TwentyUpdated");
    EXPECT_TRUE(avlInvariantsHold(root));
}

TEST_F(AvlTest, DisplayFunctionsAcceptNullAndPrintStationData) {
    testing::internal::CaptureStdout();
    displayStation(nullptr);
    displayStationsInOrder(nullptr);
    std::string emptyOutput = testing::internal::GetCapturedStdout();
    EXPECT_NE(emptyOutput.find("No stations available"), std::string::npos);

    ASSERT_EQ(insertStation(&root, makeStation(12, "DisplayMe")), APP_SUCCESS);
    testing::internal::CaptureStdout();
    displayStation(&root->station);
    displayStationsInOrder(root);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("Station ID"), std::string::npos);
    EXPECT_NE(output.find("DisplayMe"), std::string::npos);
}

TEST(StorageApi, RejectsNullArgumentsAndMissingFile) {
    StationNode* root = nullptr;
    EXPECT_EQ(loadStationsFromCSV(nullptr, "some.csv"), APP_INVALID_INPUT);
    EXPECT_EQ(loadStationsFromCSV(&root, nullptr), APP_INVALID_INPUT);
    EXPECT_EQ(loadStationsFromCSV(&root, "missing_file.csv"), APP_ERROR);
    EXPECT_EQ(saveStationsToCSV(nullptr, nullptr), APP_INVALID_INPUT);
    freeStationTree(root);
}

TEST(StorageApi, LoadsValidRowsAndReportsMalformedOrDuplicateRows) {
    StationNode* root = nullptr;
    writeText("data/storage_valid.csv",
        "id,name,distance,fare,platform\n"
        "3,Third,3.5,12.0,2\n"
        "1,First,0,0,1\r\n"
        "2,Second,2.0,7.5,4\n");
    EXPECT_EQ(loadStationsFromCSV(&root, "data/storage_valid.csv"), APP_SUCCESS);
    EXPECT_EQ(countStations(root), 3);
    EXPECT_EQ(searchStation(root, 1)->station.platform, 1);
    freeStationTree(root);
    root = nullptr;

    writeText("data/storage_invalid.csv",
        "id,name,distance,fare,platform\n"
        "1,First,1,1,1\n"
        "bad,Malformed,1,1,1\n"                 // invalid ID text
        "2147483648,IDOverflow,1,1,1\n"          // ID out of range
        "2,TooLongStationNameBeyondLimit123456789ABCDEFGHIJKLMN,1,1,1\n"
        "3,BadDistance,abc,1,1\n"               // malformed distance
        "4,InfiniteDistance,inf,1,1\n"           // non-finite distance
        "5,NegativeDistance,-1,1,1\n"
        "6,BadFare,1,abc,1\n"                   // malformed fare
        "7,InfiniteFare,1,inf,1\n"                // non-finite fare
        "8,NegativeFare,1,-1,1\n"
        "9,BadPlatform,1,1,x\n"                  // malformed platform
        "10,PlatformOverflow,1,1,2147483648\n"    // platform out of range
        "11,ZeroPlatform,1,1,0\n"
        "12,TooFewFields,1,1\n"                  // missing platform
        "13,TooMany,1,1,1,extra\n"                // extra field
        "1,Duplicate,1,1,1\n");
    EXPECT_NE(loadStationsFromCSV(&root, "data/storage_invalid.csv"), APP_SUCCESS);
    EXPECT_EQ(countStations(root), 1);
    EXPECT_NE(searchStation(root, 1), nullptr);
    freeStationTree(root);
    root = nullptr;

    // Blank rows and rows without any CSV fields must be rejected safely.
    writeText("data/storage_blank_rows.csv", "\n,\n");
    EXPECT_EQ(loadStationsFromCSV(&root, "data/storage_blank_rows.csv"), APP_ERROR);
    EXPECT_EQ(countStations(root), 0);

    // A header is optional; a first line that is a data record is parsed.
    writeText("data/storage_no_header.csv", "9,Nine,9.0,9.0,1\n");
    EXPECT_EQ(loadStationsFromCSV(&root, "data/storage_no_header.csv"), APP_SUCCESS);
    EXPECT_EQ(countStations(root), 1);
    freeStationTree(root);
    root = nullptr;

    writeText("data/storage_empty.csv", "");
    EXPECT_EQ(loadStationsFromCSV(&root, "data/storage_empty.csv"), APP_SUCCESS);
    EXPECT_EQ(countStations(root), 0);
}

TEST(StorageApi, ReportsSaveFailureAndAcceptsHeaderOnlyFile) {
    StationNode* root = nullptr;
    EXPECT_EQ(saveStationsToCSV(nullptr, nullptr), APP_INVALID_INPUT);
    EXPECT_EQ(saveStationsToCSV(root, "data/nonexistent_directory/output.csv"),
              APP_ERROR);

    writeText("data/header_only.csv", "id,name,distance,fare,platform\n");
    EXPECT_EQ(loadStationsFromCSV(&root, "data/header_only.csv"), APP_SUCCESS);
    EXPECT_EQ(countStations(root), 0);

    // CSV_LINE_MAX is 256. A physical row exceeding that buffer is rejected;
    // its remaining chunk must not accidentally become a valid record.
    std::string longRow = "1," + std::string(300, 'N') + ",1,1,1\n";
    writeText("data/overlong_row.csv", "id,name,distance,fare,platform\n" + longRow);
    EXPECT_EQ(loadStationsFromCSV(&root, "data/overlong_row.csv"), APP_ERROR);
    EXPECT_EQ(countStations(root), 0);
    freeStationTree(root);
}

TEST(StorageApi, SavesInSortedOrderAndRoundTripsEmptyAndPopulatedTrees) {
    StationNode* root = nullptr;
    EXPECT_EQ(saveStationsToCSV(root, "data/empty.csv"), APP_SUCCESS);
    EXPECT_EQ(readText("data/empty.csv"), "id,name,distance,fare,platform\n");

    ASSERT_EQ(insertStation(&root, makeStation(20, "Twenty")), APP_SUCCESS);
    ASSERT_EQ(insertStation(&root, makeStation(10, "Ten")), APP_SUCCESS);
    ASSERT_EQ(insertStation(&root, makeStation(30, "Thirty")), APP_SUCCESS);
    EXPECT_EQ(saveStationsToCSV(root, "data/roundtrip.csv"), APP_SUCCESS);
    std::string saved = readText("data/roundtrip.csv");
    EXPECT_LT(saved.find("10,Ten"), saved.find("20,Twenty"));
    EXPECT_LT(saved.find("20,Twenty"), saved.find("30,Thirty"));

    StationNode* loaded = nullptr;
    EXPECT_EQ(loadStationsFromCSV(&loaded, "data/roundtrip.csv"), APP_SUCCESS);
    EXPECT_EQ(countStations(loaded), 3);
    EXPECT_NE(searchStation(loaded, 20), nullptr);
    freeStationTree(root);
    freeStationTree(loaded);
}

TEST(AuthApi, AcceptsAdminViewerAndRejectsBadCredentials) {
    writeDefaultUsers();
    User user{};
    EXPECT_EQ(authenticateUser("admin", "admin123", &user), APP_SUCCESS);
    EXPECT_EQ(user.role, ROLE_ADMIN);
    EXPECT_STREQ(user.username, "admin");
    EXPECT_EQ(authenticateUser("viewer", "viewer123", &user), APP_SUCCESS);
    EXPECT_EQ(user.role, ROLE_VIEWER);
    EXPECT_EQ(authenticateUser("admin", "wrong", &user), APP_PERMISSION_DENIED);
    EXPECT_EQ(authenticateUser("unknown", "pass", &user), APP_PERMISSION_DENIED);
}

TEST(AuthApi, HandlesInvalidArgumentsAndFileErrors) {
    User user{};
    EXPECT_EQ(authenticateUser(nullptr, "pass", &user), APP_INVALID_INPUT);
    EXPECT_EQ(authenticateUser("user", nullptr, &user), APP_INVALID_INPUT);
    EXPECT_EQ(authenticateUser("user", "pass", nullptr), APP_INVALID_INPUT);
    EXPECT_EQ(authenticateUser("", "pass", &user), APP_INVALID_INPUT);
    EXPECT_EQ(authenticateUser("user", "", &user), APP_INVALID_INPUT);

    writeText("data/users.csv", "");
    EXPECT_EQ(authenticateUser("admin", "admin123", &user), APP_ERROR);
    fs::remove("data/users.csv");
    EXPECT_EQ(authenticateUser("admin", "admin123", &user), APP_ERROR);
    writeDefaultUsers();
}

TEST(AuthApi, SkipsMalformedRowsAndRejectsUnknownRole) {
    writeText("data/users.csv",
        "username,password,role\r\n"
        "badrow\r\n"
        "admin,secret,ADMIN,EXTRA\r\n"
        "admin,secret,ROOT\r\n"
        "viewer,viewer123,VIEWER\r\n");
    User user{};
    EXPECT_EQ(authenticateUser("viewer", "viewer123", &user), APP_SUCCESS);
    EXPECT_EQ(authenticateUser("admin", "secret", &user), APP_ERROR);

    const std::string longUsername(USERNAME_MAX + 8, 'u');
    writeText("data/users.csv",
        "username,password,role\n" + longUsername + ",pw,ADMIN\n");
    EXPECT_EQ(authenticateUser(longUsername.c_str(), "pw", &user), APP_ERROR);
    writeDefaultUsers();
}

TEST(AuthApi, CoversPermissionMatrixAndUnknownInputs) {
    EXPECT_TRUE(hasPermission(ROLE_ADMIN, "VIEW"));
    EXPECT_TRUE(hasPermission(ROLE_ADMIN, "SEARCH"));
    EXPECT_TRUE(hasPermission(ROLE_ADMIN, "ADD"));
    EXPECT_TRUE(hasPermission(ROLE_ADMIN, "UPDATE"));
    EXPECT_TRUE(hasPermission(ROLE_ADMIN, "DELETE"));
    EXPECT_TRUE(hasPermission(ROLE_ADMIN, "VALIDATE"));
    EXPECT_TRUE(hasPermission(ROLE_VIEWER, "VIEW"));
    EXPECT_TRUE(hasPermission(ROLE_VIEWER, "SEARCH"));
    EXPECT_FALSE(hasPermission(ROLE_VIEWER, "ADD"));
    EXPECT_FALSE(hasPermission(ROLE_VIEWER, "UPDATE"));
    EXPECT_FALSE(hasPermission(ROLE_VIEWER, "DELETE"));
    EXPECT_FALSE(hasPermission(ROLE_VIEWER, "VALIDATE"));
    EXPECT_FALSE(hasPermission(ROLE_ADMIN, "UNKNOWN"));
    EXPECT_FALSE(hasPermission(ROLE_ADMIN, nullptr));
    EXPECT_FALSE(hasPermission(static_cast<UserRole>(99), "VIEW"));
    EXPECT_FALSE(hasPermission(static_cast<UserRole>(99), "ADD"));
    EXPECT_FALSE(hasPermission(static_cast<UserRole>(99), "VALIDATE"));
}

TEST(AuditLogApi, RejectsNullArgumentsAndAppendsEntries) {
    EXPECT_EQ(writeAuditLog(nullptr, "LOGIN", "ok", APP_SUCCESS), APP_INVALID_INPUT);
    EXPECT_EQ(writeAuditLog("admin", nullptr, "ok", APP_SUCCESS), APP_INVALID_INPUT);
    EXPECT_EQ(writeAuditLog("admin", "LOGIN", nullptr, APP_SUCCESS), APP_INVALID_INPUT);
    writeText("data/audit.log", "");
    EXPECT_EQ(writeAuditLog("admin", "LOGIN", "Authentication successful", APP_SUCCESS), APP_SUCCESS);
    EXPECT_EQ(writeAuditLog("viewer", "DELETE", "Permission denied", APP_PERMISSION_DENIED), APP_SUCCESS);
    std::string log = readText("data/audit.log");
    EXPECT_NE(log.find("user=admin action=LOGIN details=Authentication successful result=0"), std::string::npos);
    EXPECT_NE(log.find("user=viewer action=DELETE details=Permission denied result=5"), std::string::npos);
}

TEST(AuditLogApi, ReturnsErrorWhenAuditPathCannotBeOpened) {
    fs::remove("data/audit.log");
    fs::create_directory("data/audit.log");
    EXPECT_EQ(writeAuditLog("admin", "LOGIN", "cannot write", APP_SUCCESS), APP_ERROR);
    fs::remove_all("data/audit.log");
    writeText("data/audit.log", "");
}

TEST(StationValidationThreads, NullAndEmptyReports) {
    EXPECT_EQ(generateStationValidationReport(nullptr, nullptr), APP_INVALID_INPUT);
    StationValidationReport report{};
    EXPECT_EQ(generateStationValidationReport(nullptr, &report), APP_SUCCESS);
    EXPECT_EQ(report.station_count, 0u);
    EXPECT_EQ(report.invalid_count, 0u);
    EXPECT_EQ(report.stations, nullptr);
    EXPECT_EQ(report.issue_flags, nullptr);
    testing::internal::CaptureStdout();
    displayStationValidationReport(nullptr);
    displayStationValidationReport(&report);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("report is unavailable"), std::string::npos);
    EXPECT_NE(output.find("No stations are available"), std::string::npos);
    freeStationValidationReport(nullptr);
    freeStationValidationReport(&report);
}

TEST(StationValidationThreads, ReportsValidStationsAndSplitsWorkAcrossThreads) {
    StationNode* root = nullptr;
    for (int id : {10, 20, 30, 40, 50}) {
        ASSERT_EQ(insertStation(&root, makeStation(id, "Valid")), APP_SUCCESS);
    }
    StationValidationReport report{};
    ASSERT_EQ(generateStationValidationReport(root, &report), APP_SUCCESS);
    EXPECT_EQ(report.station_count, 5u);
    EXPECT_EQ(report.invalid_count, 0u);
    EXPECT_EQ(report.worker1_checked + report.worker2_checked, 5u);
    EXPECT_EQ(report.worker1_checked, 3u);
    EXPECT_EQ(report.worker2_checked, 2u);
    for (size_t i = 0; i < report.station_count; ++i) EXPECT_EQ(report.issue_flags[i], 0u);
    testing::internal::CaptureStdout();
    displayStationValidationReport(&report);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("All station records passed validation"), std::string::npos);
    freeStationValidationReport(&report);
    freeStationTree(root);
}

TEST(StationValidationThreads, FlagsEveryInvalidFieldAndDisplaysIssues) {
    StationNode* root = nullptr;
    ASSERT_EQ(insertStation(&root, makeStation(7, "InitiallyValid")), APP_SUCCESS);
    StationNode* node = searchStation(root, 7);
    ASSERT_NE(node, nullptr);
    node->station.id = 0;
    node->station.name[0] = '\0';
    node->station.distance = std::numeric_limits<float>::quiet_NaN();
    node->station.fare = -2.0f;
    node->station.platform = 0;

    StationValidationReport report{};
    ASSERT_EQ(generateStationValidationReport(root, &report), APP_SUCCESS);
    ASSERT_EQ(report.station_count, 1u);
    EXPECT_EQ(report.invalid_count, 1u);
    EXPECT_EQ(report.issue_flags[0],
              ISSUE_INVALID_ID | ISSUE_EMPTY_NAME | ISSUE_INVALID_DISTANCE |
              ISSUE_INVALID_FARE | ISSUE_INVALID_PLATFORM);
    EXPECT_EQ(report.worker1_checked, 1u);
    EXPECT_EQ(report.worker2_checked, 0u);

    testing::internal::CaptureStdout();
    displayStationValidationReport(&report);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("Station ID must be positive"), std::string::npos);
    EXPECT_NE(output.find("Station name is empty"), std::string::npos);
    EXPECT_NE(output.find("Distance is invalid or negative"), std::string::npos);
    EXPECT_NE(output.find("Fare is invalid or negative"), std::string::npos);
    EXPECT_NE(output.find("Platform number must be positive"), std::string::npos);
    freeStationValidationReport(&report);
    freeStationTree(root);
}

TEST(StationValidationThreads, IndependentlyCoversNegativeAndNonFiniteDistanceAndFare) {
    StationNode* root = nullptr;
    ASSERT_EQ(insertStation(&root, makeStation(10, "NegativeDistance")), APP_SUCCESS);
    ASSERT_EQ(insertStation(&root, makeStation(20, "InfiniteFare")), APP_SUCCESS);
    ASSERT_EQ(insertStation(&root, makeStation(30, "NegativeFare")), APP_SUCCESS);
    ASSERT_EQ(insertStation(&root, makeStation(40, "InfiniteDistance")), APP_SUCCESS);
    ASSERT_EQ(insertStation(&root, makeStation(50, "Valid")), APP_SUCCESS);

    // Corrupt records after insertion to exercise validator branches that the
    // normal insertion API correctly prevents.
    searchStation(root, 10)->station.distance = -1.0f;
    searchStation(root, 20)->station.fare = std::numeric_limits<float>::infinity();
    searchStation(root, 30)->station.fare = -1.0f;
    searchStation(root, 40)->station.distance = std::numeric_limits<float>::infinity();

    StationValidationReport report{};
    ASSERT_EQ(generateStationValidationReport(root, &report), APP_SUCCESS);
    ASSERT_EQ(report.station_count, 5u);
    EXPECT_EQ(report.invalid_count, 4u);
    EXPECT_EQ(report.issue_flags[0], ISSUE_INVALID_DISTANCE);
    EXPECT_EQ(report.issue_flags[1], ISSUE_INVALID_FARE);
    EXPECT_EQ(report.issue_flags[2], ISSUE_INVALID_FARE);
    EXPECT_EQ(report.issue_flags[3], ISSUE_INVALID_DISTANCE);
    EXPECT_EQ(report.issue_flags[4], 0u);
    EXPECT_EQ(report.worker1_checked + report.worker2_checked, 5u);

    testing::internal::CaptureStdout();
    displayStationValidationReport(&report);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("Station records requiring attention"), std::string::npos);
    EXPECT_NE(output.find("Station ID: 10"), std::string::npos);
    EXPECT_NE(output.find("Station ID: 20"), std::string::npos);
    EXPECT_NE(output.find("Station ID: 30"), std::string::npos);
    EXPECT_NE(output.find("Station ID: 40"), std::string::npos);
    EXPECT_EQ(output.find("All station records passed validation"), std::string::npos);

    freeStationValidationReport(&report);
    freeStationTree(root);
}

TEST(StationValidationThreads, TwoStationsAreSplitOnePerWorker) {
    StationNode* root = nullptr;
    ASSERT_EQ(insertStation(&root, makeStation(20, "Twenty")), APP_SUCCESS);
    ASSERT_EQ(insertStation(&root, makeStation(10, "Ten")), APP_SUCCESS);
    StationValidationReport report{};
    ASSERT_EQ(generateStationValidationReport(root, &report), APP_SUCCESS);
    EXPECT_EQ(report.station_count, 2u);
    EXPECT_EQ(report.worker1_checked, 1u);
    EXPECT_EQ(report.worker2_checked, 1u);
    EXPECT_EQ(report.invalid_count, 0u);
    freeStationValidationReport(&report);
    freeStationTree(root);
}

/*
 * This is an end-to-end test of the actual interactive application.
 * main.c must be compiled with -Dmain=metro_app_main so GoogleTest owns main().
 * It intentionally runs last because stdin is redirected for the application.
 */
TEST(ZIntegrationTest, ExercisesLoginMenusStationCrudAndExitPaths) {
    writeDefaultStations();
    writeDefaultUsers();
    writeText("data/audit.log", "");

    const std::string script =
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n" // overlong username
        "admin\nadmin123\n"
        "abc\n"                  // invalid menu integer
        "99\n"                   // invalid menu option
        "1\n"                    // display stations
        "2\n101\n"              // found station
        "2\n999\n"              // missing station
        "3\n"                    // summary
        "4\n101\n106\n"        // valid source/destination
        "4\n999\n106\n"        // missing source
        "4\n101\n999\n"        // missing destination
        "4\n101\n101\n"        // same source/destination
        "4\n-1\n106\n"         // invalid ID
        "5\n"                    // multithreaded station validation
        "6\n108\n\n"           // add: empty station name
        "6\n108\nName,WithComma\n" // add: comma in name
        "6\n108\nTemp\nnot-a-number\n" // add: invalid float
        "6\n108\nTemp\n-1\n12\n2\n" // add: negative distance
        "6\n108\nTemp\n1\n-1\n2\n" // add: negative fare
        "6\n108\nTemp\n1\n12\n0\n" // add: invalid platform
        "6\n0\nZeroId\n1\n1\n1\n" // add: invalid ID
        "6\n107\nTestStop\n2.5\n12.5\n2\n" // add
        "6\n107\nDuplicate\n2.5\n12.5\n2\n" // duplicate add
        "7\n999\n"              // update missing station
        "7\n107\nUpdatedStop\n3.5\n13.5\n3\n" // update
        "8\n999\n"              // delete missing station
        "8\n107\n"              // delete
        "9\n"                    // logout
        "viewer\nviewer123\n"
        "5\n"                    // viewer cannot run admin validation
        "6\n"                    // viewer cannot add station
        "7\n"                    // viewer cannot update station
        "8\n"                    // viewer cannot delete station
        "2\n999\n"              // viewer search missing station
        "9\n"                    // logout
        "baduser\nbadpass\n"   // failed login
        "admin\nadmin123\n"
        "0\n";                  // exit

    fs::path inputPath = testRoot / "app_input.txt";
    writeText(inputPath, script);
#if defined(_WIN32)
    int savedStdin = _dup(_fileno(stdin));
#else
    int savedStdin = dup(fileno(stdin));
#endif
    ASSERT_GE(savedStdin, 0);
    ASSERT_NE(std::freopen(inputPath.string().c_str(), "r", stdin), nullptr);

    testing::internal::CaptureStdout();
    int result = metro_app_main();
    std::string output = testing::internal::GetCapturedStdout();

#if defined(_WIN32)
    ASSERT_EQ(_dup2(savedStdin, _fileno(stdin)), 0);
    _close(savedStdin);
#else
    ASSERT_EQ(dup2(savedStdin, fileno(stdin)), fileno(stdin));
    close(savedStdin);
#endif

    EXPECT_EQ(result, EXIT_SUCCESS);
    EXPECT_NE(output.find("Loaded 6 stations"), std::string::npos);
    EXPECT_NE(output.find("STATION DATA VALIDATION REPORT"), std::string::npos);
    EXPECT_NE(output.find("All station records passed validation"), std::string::npos);
    EXPECT_NE(output.find("Station added and saved successfully"), std::string::npos);
    EXPECT_NE(output.find("A station with that ID already exists"), std::string::npos);
    EXPECT_NE(output.find("Station updated and saved successfully"), std::string::npos);
    EXPECT_NE(output.find("Station deleted and changes saved successfully"), std::string::npos);
    EXPECT_NE(output.find("Invalid option for Viewer"), std::string::npos);
    EXPECT_NE(output.find("Validation result: SUCCESS"), std::string::npos);
    EXPECT_NE(output.find("Source station ID 999 was not found"), std::string::npos);
    EXPECT_NE(output.find("Destination station ID 999 was not found"), std::string::npos);
    EXPECT_NE(output.find("Name cannot be empty or contain a comma"), std::string::npos);
    EXPECT_NE(output.find("Invalid number. Please try again."), std::string::npos);
    EXPECT_NE(output.find("ID and platform must be positive"), std::string::npos);
    EXPECT_NE(output.find("Login failed"), std::string::npos);
    EXPECT_NE(output.find("Metro Management System closed"), std::string::npos);
    EXPECT_EQ(countStations(nullptr), 0); // tree is owned/freed by the application
}
TEST(ZIntegrationTest, ExitsCleanlyWhenInputEndsAtLoginPrompt) {
    writeDefaultStations();
    writeDefaultUsers();
    writeText("data/audit.log", "");
    const fs::path inputPath = testRoot / "empty_input.txt";
    writeText(inputPath, "");

#if defined(_WIN32)
    int savedStdin = _dup(_fileno(stdin));
#else
    int savedStdin = dup(fileno(stdin));
#endif
    ASSERT_GE(savedStdin, 0);
    ASSERT_NE(std::freopen(inputPath.string().c_str(), "r", stdin), nullptr);

    testing::internal::CaptureStdout();
    int result = metro_app_main();
    std::string output = testing::internal::GetCapturedStdout();

#if defined(_WIN32)
    ASSERT_EQ(_dup2(savedStdin, _fileno(stdin)), 0);
    _close(savedStdin);
#else
    ASSERT_EQ(dup2(savedStdin, fileno(stdin)), fileno(stdin));
    close(savedStdin);
#endif

    EXPECT_EQ(result, EXIT_SUCCESS);
    EXPECT_NE(output.find("Loaded 6 stations"), std::string::npos);
    EXPECT_NE(output.find("Username:"), std::string::npos);
    EXPECT_NE(output.find("Metro Management System closed"), std::string::npos);
}

TEST(ZIntegrationTest, FailsStartupWhenStationCsvContainsInvalidRecord) {
    writeText("data/stations.csv",
        "id,name,distance,fare,platform\n"
        "101,Central,1.0,10.0,1\n"
        "bad,Broken,1.0,10.0,1\n");
    writeDefaultUsers();
    writeText("data/audit.log", "");

    // main exits before reading stdin when the station file is invalid.
    const fs::path inputPath = testRoot / "startup_input.txt";
    writeText(inputPath, "");
#if defined(_WIN32)
    int savedStdin = _dup(_fileno(stdin));
#else
    int savedStdin = dup(fileno(stdin));
#endif
    ASSERT_GE(savedStdin, 0);
    ASSERT_NE(std::freopen(inputPath.string().c_str(), "r", stdin), nullptr);

    int result = metro_app_main();

#if defined(_WIN32)
    ASSERT_EQ(_dup2(savedStdin, _fileno(stdin)), 0);
    _close(savedStdin);
#else
    ASSERT_EQ(dup2(savedStdin, fileno(stdin)), fileno(stdin));
    close(savedStdin);
#endif

    EXPECT_EQ(result, EXIT_FAILURE);
    writeDefaultStations();
}


/*
 * Helper for focused integration tests. Each call gets a fresh input stream
 * and restores stdin afterwards so one test cannot consume another test's input.
 */
struct AppRunResult {
    int exitCode;
    std::string output;
};

AppRunResult runAppWithInput(const std::string& input,
                             const std::string& filename) {
    writeDefaultStations();
    writeDefaultUsers();
    // Replace an earlier audit.log directory/file with a clean log file.
    std::error_code ec;
    fs::remove_all("data/audit.log", ec);
    writeText("data/audit.log", "");

    const fs::path inputPath = testRoot / filename;
    writeText(inputPath, input);

#if defined(_WIN32)
    int savedStdin = _dup(_fileno(stdin));
#else
    int savedStdin = dup(fileno(stdin));
#endif
    EXPECT_GE(savedStdin, 0);
    if (savedStdin < 0) {
        return {EXIT_FAILURE, ""};
    }

    if (std::freopen(inputPath.string().c_str(), "r", stdin) == nullptr) {
#if defined(_WIN32)
        _close(savedStdin);
#else
        close(savedStdin);
#endif
        ADD_FAILURE() << "Could not redirect stdin to " << inputPath.string();
        return {EXIT_FAILURE, ""};
    }

    testing::internal::CaptureStdout();
    const int result = metro_app_main();
    const std::string output = testing::internal::GetCapturedStdout();

#if defined(_WIN32)
    const int restoreResult = _dup2(savedStdin, _fileno(stdin));
    _close(savedStdin);
    EXPECT_EQ(restoreResult, 0);
#else
    const int restoreResult = dup2(savedStdin, fileno(stdin));
    close(savedStdin);
    EXPECT_EQ(restoreResult, fileno(stdin));
#endif
    clearerr(stdin);

    return {result, output};
}

TEST(ZIntegrationTest, ExitsCleanlyWhenInputEndsAtPasswordPrompt) {
    const AppRunResult run = runAppWithInput(
        "admin\n", "eof_password_input.txt");

    EXPECT_EQ(run.exitCode, EXIT_SUCCESS);
    EXPECT_NE(run.output.find("Username:"), std::string::npos);
    EXPECT_NE(run.output.find("Password:"), std::string::npos);
    EXPECT_NE(run.output.find("Metro Management System closed"),
              std::string::npos);
}

TEST(ZIntegrationTest, ExitsCleanlyWhenInputEndsAtMenuPrompt) {
    const AppRunResult run = runAppWithInput(
        "admin\nadmin123\n", "eof_menu_input.txt");

    EXPECT_EQ(run.exitCode, EXIT_SUCCESS);
    EXPECT_NE(run.output.find("Welcome, admin"), std::string::npos);
    EXPECT_NE(run.output.find("Choose an option:"), std::string::npos);
    EXPECT_NE(run.output.find("Metro Management System closed"),
              std::string::npos);
}

TEST(ZIntegrationTest, HandlesEofAtSourceAndDestinationPrompts) {
    const AppRunResult sourceEof = runAppWithInput(
        "admin\nadmin123\n4\n", "eof_source_input.txt");
    EXPECT_EQ(sourceEof.exitCode, EXIT_SUCCESS);
    EXPECT_NE(sourceEof.output.find("Enter source station ID:"),
              std::string::npos);
    EXPECT_NE(sourceEof.output.find("Metro Management System closed"),
              std::string::npos);

    const AppRunResult destinationEof = runAppWithInput(
        "admin\nadmin123\n4\n101\n", "eof_destination_input.txt");
    EXPECT_EQ(destinationEof.exitCode, EXIT_SUCCESS);
    EXPECT_NE(destinationEof.output.find("Enter destination station ID:"),
              std::string::npos);
    EXPECT_NE(destinationEof.output.find("Metro Management System closed"),
              std::string::npos);
}

TEST(ZIntegrationTest, EmptyTreeSummaryAndValidationPaths) {
    std::string script =
        "admin\nadmin123\n"
        "8\n101\n"
        "8\n102\n"
        "8\n103\n"
        "8\n104\n"
        "8\n105\n"
        "8\n106\n"
        "3\n"  // summary for empty tree
        "4\n"  // source/destination validation for empty tree
        "5\n"  // multithreaded validation for empty tree
        "0\n";

    const AppRunResult run = runAppWithInput(script, "empty_tree_input.txt");
    EXPECT_EQ(run.exitCode, EXIT_SUCCESS);
    EXPECT_NE(run.output.find("Total stations: 0"), std::string::npos);
    EXPECT_NE(run.output.find(
                  "Minimum station ID: unavailable (tree is empty)"),
              std::string::npos);
    EXPECT_NE(run.output.find(
                  "Maximum station ID: unavailable (tree is empty)"),
              std::string::npos);
    EXPECT_NE(run.output.find("Validation failed: No stations are available"),
              std::string::npos);
    EXPECT_NE(run.output.find("No stations are available"),
              std::string::npos);
    EXPECT_NE(run.output.find("Metro Management System closed"),
              std::string::npos);
}

TEST(ZIntegrationTest, CoversIntegerAndFloatingPointParsingBoundaries) {
    const std::string script =
        "admin\nadmin123\n"
        "2147483648\n"                 // beyond signed int range
        "999999999999999999999999\n"   // strtol range error
        "3\n"
        "6\n108\nTemp\nnan\n"          // non-finite float
        "6\n108\nTemp\ninf\n"          // non-finite float
        "6\n108\nTemp\n12abc\n"        // trailing characters
        "6\n108\nTemp\n1e9999\n"       // strtof range error
        "6\n108\nTemp\n\n"             // empty float
        "6\n108\nTemp\n1\n12\n0\n"     // non-positive platform
        "0\n";

    const AppRunResult run = runAppWithInput(script, "numeric_boundaries_input.txt");
    EXPECT_EQ(run.exitCode, EXIT_SUCCESS);
    EXPECT_NE(run.output.find("Invalid integer. Please try again."),
              std::string::npos);
    EXPECT_NE(run.output.find("Invalid number. Please try again."),
              std::string::npos);
    EXPECT_NE(run.output.find("Metro Management System closed"),
              std::string::npos);
}

TEST(ZIntegrationTest, HandlesLongPasswordAndLongStationName) {
    const std::string longPassword(200, 'p');
    const std::string longName(200, 'N');
    const std::string script =
        "admin\n" + longPassword + "\n" // overlong password rejected
        "admin\nadmin123\n"
        "6\n108\n" + longName + "\n"   // overlong station name rejected
        "6\n108\nValidName\n1\n10\n1\n"
        "0\n";

    const AppRunResult run = runAppWithInput(script, "long_input_boundaries.txt");
    EXPECT_EQ(run.exitCode, EXIT_SUCCESS);
    EXPECT_NE(run.output.find("Input is too long. Please try again."),
              std::string::npos);
    EXPECT_NE(run.output.find("Station added and saved successfully."),
              std::string::npos);
    EXPECT_NE(run.output.find("Metro Management System closed"),
              std::string::npos);
}

TEST(ZIntegrationTest, HandlesInvalidSearchUpdateDeleteAndDestinationInputs) {
    const std::string script =
        "admin\nadmin123\n"
        "2\nnot-an-id\n"       // invalid integer during search
        "7\n101\nUpdated\nbad-distance\n" // invalid update field
        "8\nabc\n"              // invalid integer during delete
        "4\n101\n-2\n"          // non-positive destination ID
        "4\nabc\n"             // invalid source integer, then EOF at menu
        "0\n";

    const AppRunResult run = runAppWithInput(script, "invalid_operation_inputs.txt");
    EXPECT_EQ(run.exitCode, EXIT_SUCCESS);
    EXPECT_NE(run.output.find("Invalid integer. Please try again."),
              std::string::npos);
    EXPECT_NE(run.output.find("Validation failed: Station IDs must be positive."),
              std::string::npos);
    EXPECT_NE(run.output.find("Metro Management System closed"),
              std::string::npos);
}

TEST(ZIntegrationTest, ReportsAuditLogFailureWithoutCrashingApplication) {
    writeDefaultStations();
    writeDefaultUsers();
    fs::remove_all("data/audit.log");
    fs::create_directory("data/audit.log");

    const fs::path inputPath = testRoot / "audit_failure_input.txt";
    writeText(inputPath, "admin\nadmin123\n0\n");

#if defined(_WIN32)
    int savedStdin = _dup(_fileno(stdin));
#else
    int savedStdin = dup(fileno(stdin));
#endif
    ASSERT_GE(savedStdin, 0);
    ASSERT_NE(std::freopen(inputPath.string().c_str(), "r", stdin), nullptr);

    testing::internal::CaptureStderr();
    testing::internal::CaptureStdout();
    const int result = metro_app_main();
    const std::string output = testing::internal::GetCapturedStdout();
    const std::string errors = testing::internal::GetCapturedStderr();

#if defined(_WIN32)
    ASSERT_EQ(_dup2(savedStdin, _fileno(stdin)), 0);
    _close(savedStdin);
#else
    ASSERT_EQ(dup2(savedStdin, fileno(stdin)), fileno(stdin));
    close(savedStdin);
#endif
    clearerr(stdin);

    EXPECT_EQ(result, EXIT_SUCCESS);
    EXPECT_NE(output.find("Welcome, admin"), std::string::npos);
    EXPECT_NE(output.find("Metro Management System closed"), std::string::npos);
    EXPECT_NE(errors.find("Warning: could not write to the audit log."),
              std::string::npos);

    fs::remove_all("data/audit.log");
    writeText("data/audit.log", "");
}


/*
 * Additional branch-focused tests based on the current source files.
 * The /dev/full and directory-read cases are Linux/POSIX-specific.
 */

TEST(StorageApi, LoadingDirectoryReportsReadErrorOnPosix) {
#if !defined(_WIN32)
    StationNode* root = nullptr;

    // fopen() can open a directory on Linux, but fgets() fails with EISDIR.
    EXPECT_EQ(loadStationsFromCSV(&root, "data"), APP_ERROR);
    EXPECT_EQ(root, nullptr);
    freeStationTree(root);
#else
    GTEST_SKIP() << "Directory read-error behaviour is POSIX-specific.";
#endif
}

TEST(StorageApi, ReportsFlushFailureWhenWritingToFullDeviceOnPosix) {
#if !defined(_WIN32)
    StationNode* root = nullptr;
    const fs::path fullPath = "data/full_device.csv";
    std::error_code ec;

    fs::remove(fullPath, ec);
    fs::create_symlink("/dev/full", fullPath, ec);
    ASSERT_FALSE(ec) << "Could not create /dev/full symlink: " << ec.message();

    // The buffered header write may succeed, but flushing/closing /dev/full
    // fails. saveStationsToCSV must propagate that failure.
    EXPECT_EQ(saveStationsToCSV(root, fullPath.string().c_str()), APP_ERROR);

    fs::remove(fullPath, ec);
    freeStationTree(root);
#else
    GTEST_SKIP() << "/dev/full is Linux/POSIX-specific.";
#endif
}

TEST(AuthApi, SkipsRowsWithMissingOrExtraColumnsBeforeValidUser) {
    writeText("data/users.csv",
        "username,password,role\n"
        "missing,password\n"
        "extra,password,VIEWER,unexpected\n"
        ",emptyusername,VIEWER\n"
        "viewer,viewer123,VIEWER\n");

    User user{};
    EXPECT_EQ(authenticateUser("viewer", "viewer123", &user), APP_SUCCESS);
    EXPECT_EQ(user.role, ROLE_VIEWER);
    EXPECT_STREQ(user.username, "viewer");
    EXPECT_EQ(authenticateUser("missing", "password", &user),
              APP_PERMISSION_DENIED);

    writeDefaultUsers();
}

TEST(AuditLogApi, DetectsFlushFailureWhenAuditPathTargetsFullDeviceOnPosix) {
#if !defined(_WIN32)
    const fs::path auditPath = "data/audit.log";
    std::error_code ec;

    fs::remove_all(auditPath, ec);
    fs::create_symlink("/dev/full", auditPath, ec);
    ASSERT_FALSE(ec) << "Could not create /dev/full symlink: " << ec.message();

    EXPECT_EQ(writeAuditLog("admin", "LOGIN", "full device test", APP_SUCCESS),
              APP_ERROR);

    fs::remove(auditPath, ec);
    writeText(auditPath, "");
#else
    GTEST_SKIP() << "/dev/full is Linux/POSIX-specific.";
#endif
}

TEST(ZIntegrationTest, ViewerIsRejectedFromEveryAdminOnlyMenuOption) {
    const std::string script =
        "viewer\nviewer123\n"
        "5\n"  // threaded station validation
        "6\n"  // add
        "7\n"  // update
        "8\n"  // delete
        "9\n"  // logout
        "admin\nadmin123\n"
        "0\n";

    const AppRunResult run = runAppWithInput(
        script, "viewer_admin_options_input.txt");

    EXPECT_EQ(run.exitCode, EXIT_SUCCESS);
    const std::string expected = "Invalid option for Viewer.";
    size_t count = 0;
    size_t position = 0;
    while ((position = run.output.find(expected, position)) != std::string::npos) {
        ++count;
        position += expected.size();
    }
    EXPECT_EQ(count, 4u);
    EXPECT_NE(run.output.find("Metro Management System closed"),
              std::string::npos);
}

TEST(ZIntegrationTest, ReportsBothSourceAndDestinationMissing) {
    const AppRunResult run = runAppWithInput(
        "admin\nadmin123\n"
        "4\n999\n998\n"
        "0\n",
        "both_route_stations_missing.txt");

    EXPECT_EQ(run.exitCode, EXIT_SUCCESS);
    EXPECT_NE(run.output.find("Source station ID 999 was not found."),
              std::string::npos);
    EXPECT_NE(run.output.find("Destination station ID 998 was not found."),
              std::string::npos);
    EXPECT_NE(run.output.find("Validation result: FAILED."),
              std::string::npos);
}

TEST(ZIntegrationTest, RejectsBlankAndCommaContainingStationNames) {
    const AppRunResult run = runAppWithInput(
        "admin\nadmin123\n"
        "6\n108\n\n"                 // blank name
        "6\n108\nName,WithComma\n"   // comma in name
        "6\n108\nValidName\n1\n10\n1\n"
        "0\n",
        "station_name_rejections.txt");

    EXPECT_EQ(run.exitCode, EXIT_SUCCESS);
    EXPECT_NE(run.output.find("Name cannot be empty or contain a comma."),
              std::string::npos);
    EXPECT_NE(run.output.find("Station added and saved successfully."),
              std::string::npos);
    EXPECT_NE(run.output.find("Metro Management System closed"),
              std::string::npos);
}



/* Targeted additions from source review: unique edge paths, not duplicate CRUD tests. */
TEST(StorageBoundaryCoverage, ExactBufferSizedFinalRowAtEofIsHandledOnce) {
    StationNode* root = nullptr;
    // CSV_LINE_MAX is 256: 255 bytes with no newline makes fgets hit the
    // buffer boundary, then fgetc sees EOF. The row must be parsed once.
    const std::string row = "1," + std::string(253, 'X');
    ASSERT_EQ(row.size(), 255u);
    writeText("data/exact_buffer_final_row.csv", row);
    EXPECT_EQ(loadStationsFromCSV(&root, "data/exact_buffer_final_row.csv"), APP_ERROR);
    EXPECT_EQ(countStations(root), 0);
    freeStationTree(root);
}

TEST(StorageBoundaryCoverage, OverlongRowDoesNotConsumeFollowingValidRow) {
    StationNode* root = nullptr;
    const std::string overlong = "1," + std::string(300, 'X') + ",1,1,1\n";
    writeText("data/overlong_then_valid.csv",
              "id,name,distance,fare,platform\n" + overlong +
              "2,ValidAfterLongRow,2.0,3.0,1\n");
    EXPECT_EQ(loadStationsFromCSV(&root, "data/overlong_then_valid.csv"), APP_ERROR);
    EXPECT_EQ(countStations(root), 1);
    ASSERT_NE(searchStation(root, 2), nullptr);
    EXPECT_STREQ(searchStation(root, 2)->station.name, "ValidAfterLongRow");
    freeStationTree(root);
}

TEST(StationValidationBoundaryCoverage, OddStationCountSplitsWorkAcrossThreads) {
    StationNode* root = nullptr;
    ASSERT_EQ(insertStation(&root, makeStation(30, "Thirty")), APP_SUCCESS);
    ASSERT_EQ(insertStation(&root, makeStation(10, "Ten")), APP_SUCCESS);
    ASSERT_EQ(insertStation(&root, makeStation(20, "Twenty")), APP_SUCCESS);
    StationValidationReport report{};
    ASSERT_EQ(generateStationValidationReport(root, &report), APP_SUCCESS);
    EXPECT_EQ(report.station_count, 3u);
    EXPECT_EQ(report.worker1_checked, 2u);
    EXPECT_EQ(report.worker2_checked, 1u);
    EXPECT_EQ(report.invalid_count, 0u);
    freeStationValidationReport(&report);
    freeStationTree(root);
}

TEST(StationValidationBoundaryCoverage, DisplayHandlesAllValidReportAndNullReport) {
    StationNode* root = nullptr;
    ASSERT_EQ(insertStation(&root, makeStation(10, "Ten")), APP_SUCCESS);
    StationValidationReport report{};
    ASSERT_EQ(generateStationValidationReport(root, &report), APP_SUCCESS);
    testing::internal::CaptureStdout();
    displayStationValidationReport(&report);
    const std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("All station records passed validation"), std::string::npos);
    EXPECT_EQ(output.find("Station records requiring attention"), std::string::npos);
    testing::internal::CaptureStdout();
    displayStationValidationReport(nullptr);
    const std::string nullOutput = testing::internal::GetCapturedStdout();
    EXPECT_NE(nullOutput.find("Error: validation report is unavailable"), std::string::npos);
    freeStationValidationReport(&report);
    freeStationTree(root);
}

} // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    return RUN_ALL_TESTS();
}

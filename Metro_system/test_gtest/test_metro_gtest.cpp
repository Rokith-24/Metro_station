#include <gtest/gtest.h>

#include <cstdio>
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
}

TEST_F(AvlTest, UpdateValidatesIdDataAndMissingStations) {
    Station updated = makeStation(5, "Updated", 9.0f, 25.0f, 4);
    EXPECT_EQ(updateStation(nullptr, 5, updated), APP_NOT_FOUND);
    EXPECT_EQ(updateStation(root, 0, updated), APP_INVALID_INPUT);
    EXPECT_EQ(updateStation(root, 5, makeStation(6, "Mismatch")), APP_INVALID_INPUT);
    EXPECT_EQ(updateStation(root, 5, makeStation(5, "")), APP_INVALID_INPUT);
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
        "bad,Malformed,1,1,1\n"
        "1,Duplicate,1,1,1\n"
        "2,NegativeDistance,-1,1,1\n"
        "3,TooMany,1,1,1,extra\n");
    EXPECT_NE(loadStationsFromCSV(&root, "data/storage_invalid.csv"), APP_SUCCESS);
    EXPECT_EQ(countStations(root), 1);
    EXPECT_NE(searchStation(root, 1), nullptr);
    freeStationTree(root);
    root = nullptr;

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
        "username,password,role\n"
        "badrow\n"
        "admin,secret,ROOT\n"
        "viewer,viewer123,VIEWER\n");
    User user{};
    EXPECT_EQ(authenticateUser("viewer", "viewer123", &user), APP_SUCCESS);
    EXPECT_EQ(authenticateUser("admin", "secret", &user), APP_ERROR);
    writeDefaultUsers();
}

TEST(AuthApi, CoversPermissionMatrixAndUnknownInputs) {
    EXPECT_TRUE(hasPermission(ROLE_ADMIN, "VIEW"));
    EXPECT_TRUE(hasPermission(ROLE_ADMIN, "SEARCH"));
    EXPECT_TRUE(hasPermission(ROLE_ADMIN, "ADD"));
    EXPECT_TRUE(hasPermission(ROLE_ADMIN, "UPDATE"));
    EXPECT_TRUE(hasPermission(ROLE_ADMIN, "DELETE"));
    EXPECT_TRUE(hasPermission(ROLE_VIEWER, "VIEW"));
    EXPECT_TRUE(hasPermission(ROLE_VIEWER, "SEARCH"));
    EXPECT_FALSE(hasPermission(ROLE_VIEWER, "ADD"));
    EXPECT_FALSE(hasPermission(ROLE_VIEWER, "UPDATE"));
    EXPECT_FALSE(hasPermission(ROLE_VIEWER, "DELETE"));
    EXPECT_FALSE(hasPermission(ROLE_ADMIN, "UNKNOWN"));
    EXPECT_FALSE(hasPermission(ROLE_ADMIN, nullptr));
    EXPECT_FALSE(hasPermission(static_cast<UserRole>(99), "VIEW"));
    EXPECT_FALSE(hasPermission(static_cast<UserRole>(99), "ADD"));
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
        "4\n101\n101\n"        // same source/destination
        "4\n-1\n106\n"         // invalid ID
        "5\n108\n\n"           // empty station name
        "5\n108\nTemp\nnot-a-number\n" // invalid float
        "5\n107\nTestStop\n2.5\n12.5\n2\n" // add
        "6\n107\nUpdatedStop\n3.5\n13.5\n3\n" // update
        "7\n107\n"              // delete
        "8\n"                   // logout
        "viewer\nviewer123\n"
        "5\n"                   // viewer cannot use admin option
        "2\n999\n"              // viewer search missing station
        "8\n"
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
    EXPECT_NE(output.find("Station added and saved successfully"), std::string::npos);
    EXPECT_NE(output.find("Station updated and saved successfully"), std::string::npos);
    EXPECT_NE(output.find("Station deleted and changes saved successfully"), std::string::npos);
    EXPECT_NE(output.find("Invalid option for Viewer"), std::string::npos);
    EXPECT_NE(output.find("Login failed"), std::string::npos);
    EXPECT_NE(output.find("Metro Management System closed"), std::string::npos);
    EXPECT_EQ(countStations(nullptr), 0); // tree is owned/freed by the application
}
} // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    return RUN_ALL_TESTS();
}

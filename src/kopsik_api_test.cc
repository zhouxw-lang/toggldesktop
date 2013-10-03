// Copyright 2013 Tanel Lebedev

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "./kopsik_api.h"
#include "./https_client.h"

#include "Poco/FileStream.h"
#include "Poco/File.h"

#define TESTDB "test.db"
#define ERRLEN 1024

namespace kopsik {

    class MockHTTPSClient : public HTTPSClient {
    public:
        MOCK_METHOD0(ListenToWebsocket, kopsik::error());
        MOCK_METHOD5(PostJSON, kopsik::error(
            std::string relative_url,
            std::string json,
            std::string basic_auth_username,
            std::string basic_auth_password,
            std::string *response_body));
        MOCK_METHOD4(GetJSON, kopsik::error(
            std::string relative_url,
            std::string basic_auth_username,
            std::string basic_auth_password,
            std::string *response_body));
    };

    TEST(KopsikApiTest, kopsik_context_init) {
        KopsikContext *ctx = kopsik_context_init();
        ASSERT_TRUE(ctx);
        kopsik_context_clear(ctx);
    }

    TEST(KopsikApiTest, kopsik_version) {
        int major = 0;
        int minor = 0;
        int patch = 0;
        kopsik_version(&major, &minor, &patch);
        ASSERT_TRUE(major || minor || patch);
    }

    TEST(KopsikApiTest, kopsik_set_proxy) {
        KopsikContext *ctx = kopsik_context_init();
        kopsik_set_proxy(ctx, "localhost", 8000, "johnsmith", "secret");
        ASSERT_TRUE(true);
        kopsik_context_clear(ctx);
    }

    TEST(KopsikApiTest, kopsik_set_db_path) {
        KopsikContext *ctx = kopsik_context_init();
        {
            Poco::File f(TESTDB);
            if (f.exists()) f.remove(false);
        }
        kopsik_set_db_path(ctx, TESTDB);
        kopsik_context_clear(ctx);
        Poco::File f(TESTDB);
        ASSERT_TRUE(f.exists());
    }

    TEST(KopsikApiTest, kopsik_set_log_path) {
        KopsikContext *ctx = kopsik_context_init();
        kopsik_set_log_path(ctx, "test.log");
        ASSERT_TRUE(true);
        kopsik_context_clear(ctx);
    }

    TEST(KopsikApiTest, kopsik_user_init) {
        KopsikUser *user = kopsik_user_init();
        ASSERT_TRUE(user);
        kopsik_user_clear(user);
    }

    TEST(KopsikApiTest, kopsik_set_api_token) {
        KopsikContext *ctx = kopsik_context_init();
        Poco::File f(TESTDB);
        if (f.exists()) f.remove(false);
        kopsik_set_db_path(ctx, TESTDB);
        char err[ERRLEN];
        ASSERT_EQ(KOPSIK_API_SUCCESS,
            kopsik_set_api_token(ctx, err, ERRLEN, "token"));
        const int kMaxStrLen = 10;
        char str[kMaxStrLen];
        ASSERT_EQ(KOPSIK_API_SUCCESS,
            kopsik_get_api_token(ctx, err, ERRLEN, str, kMaxStrLen));
        ASSERT_EQ("token", std::string(str));
        kopsik_context_clear(ctx);
    }

    TEST(KopsikApiTest, kopsik_lifecycle) {
        KopsikContext *ctx = kopsik_context_init();

        Poco::File f(TESTDB);
        if (f.exists()) f.remove(false);
        kopsik_set_db_path(ctx, TESTDB);

        kopsik::HTTPSClient *client =
            reinterpret_cast<HTTPSClient *>(ctx->https_client);
        delete client;
        MockHTTPSClient *mock_client = new MockHTTPSClient();
        ctx->https_client = mock_client;

        Poco::FileStream fis("testdata/me.json", std::ios::binary);
        std::stringstream ss;
        ss << fis.rdbuf();
        fis.close();
        std::string json = ss.str();

        // Login
        EXPECT_CALL(*mock_client, GetJSON(
            std::string("/api/v8/me?with_related_data=true"),
            std::string("foo@bar.com"),
            std::string("secret"),
            testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgPointee<3>(json),
            testing::Return("")));
        char err[ERRLEN];
        ASSERT_EQ(KOPSIK_API_SUCCESS, kopsik_login(
            ctx,
            err, ERRLEN,
            "foo@bar.com", "secret"));

        // We should have the API token now
        const int kMaxStrLen = 100;
        char str[kMaxStrLen];
        ASSERT_EQ(KOPSIK_API_SUCCESS,
            kopsik_get_api_token(ctx, err, ERRLEN, str, kMaxStrLen));
        ASSERT_EQ("30eb0ae954b536d2f6628f7fec47beb6", std::string(str));

        // We should have current user now
        KopsikUser *user = kopsik_user_init();
        ASSERT_EQ(KOPSIK_API_SUCCESS,
            kopsik_current_user(ctx, err, ERRLEN, user));
        ASSERT_EQ((unsigned int)10471231, user->ID);
        ASSERT_EQ(std::string("John Smith"), std::string(user->Fullname));
        kopsik_user_clear(user);

        // Count time entry items before start. It should be 3, since
        // there are 3 time entries in the me.json file we're using:
        KopsikTimeEntryViewItemList *list =
            kopsik_time_entry_view_item_list_init();
        ASSERT_EQ(KOPSIK_API_SUCCESS, kopsik_time_entry_view_items(
            ctx, err, ERRLEN, list));
        ASSERT_EQ((unsigned int)3, list->Length);
        int number_of_items = list->Length;
        kopsik_time_entry_view_item_list_clear(list);

        // Start tracking
        KopsikTimeEntryViewItem *item = kopsik_time_entry_view_item_init();
        ASSERT_EQ(KOPSIK_API_SUCCESS,
            kopsik_start(ctx, err, ERRLEN, "Test", item));
        ASSERT_EQ(std::string("Test"), std::string(item->Description));
        kopsik_time_entry_view_item_clear(item);

        // We should now have a running time entry
        int is_tracking = 0;
        KopsikTimeEntryViewItem *running = kopsik_time_entry_view_item_init();
        ASSERT_EQ(KOPSIK_API_SUCCESS, kopsik_running_time_entry_view_item(
            ctx, err, ERRLEN, running, &is_tracking));
        ASSERT_TRUE(is_tracking);
        ASSERT_GT(0, running->DurationInSeconds);
        ASSERT_EQ(std::string("Test"), std::string(running->Description));
        ASSERT_FALSE(running->Project);
        ASSERT_TRUE(running->Duration);
        ASSERT_FALSE(running->Color);
        ASSERT_TRUE(running->GUID);
        std::string GUID(running->GUID);
        kopsik_time_entry_view_item_clear(running);

        // The running time entry should *not* be listed
        // among time entry view items.
        list = kopsik_time_entry_view_item_list_init();
        ASSERT_EQ(KOPSIK_API_SUCCESS, kopsik_time_entry_view_items(
            ctx, err, ERRLEN, list));
        ASSERT_TRUE(list);
        ASSERT_EQ((unsigned int)number_of_items + 0, list->Length);
        kopsik_time_entry_view_item_list_clear(list);

        // Stop the time entry
        KopsikTimeEntryViewItem *stopped = kopsik_time_entry_view_item_init();
        ASSERT_EQ(KOPSIK_API_SUCCESS,
            kopsik_stop(ctx, err, ERRLEN, stopped));
        ASSERT_EQ(std::string("Test"), std::string(stopped->Description));
        std::string dirty_guid(stopped->GUID);
        kopsik_time_entry_view_item_clear(stopped);

        // Now the stopped time entry should be listed
        // among time entry view items.
        list = kopsik_time_entry_view_item_list_init();
        ASSERT_EQ(KOPSIK_API_SUCCESS, kopsik_time_entry_view_items(
            ctx, err, ERRLEN, list));
        ASSERT_TRUE(list);
        ASSERT_EQ((unsigned int)number_of_items + 1, list->Length);
        kopsik_time_entry_view_item_list_clear(list);

        // We should no longer get a running time entry from API.
        is_tracking = 0;
        running = kopsik_time_entry_view_item_init();
        ASSERT_EQ(KOPSIK_API_SUCCESS, kopsik_running_time_entry_view_item(
            ctx, err, ERRLEN, running, &is_tracking));
        ASSERT_FALSE(is_tracking);
        kopsik_time_entry_view_item_clear(running);

        // We started and stopped one time entry.
        // This means we should have one dirty model now.
        KopsikDirtyModels dirty_models;
        ASSERT_EQ(KOPSIK_API_SUCCESS, kopsik_pushable_models(
            ctx, err, ERRLEN, &dirty_models));
        ASSERT_EQ((unsigned int)1, dirty_models.TimeEntries);

        // Push changes
        std::stringstream response_body;
        response_body
            << "["
            << "{"
            << "\"status\": 200,"
            << "\"guid\": \"" << dirty_guid << "\","
            << "\"content_type\": \"application/json\","
            << "\"body\": \""
                << "{\"data\": {\"id\": 123456789, \"ui_modified_at\": "
                << time(0) << "}}\""
            << "}"
            << "]";
        std::string response_json = response_body.str();
        EXPECT_CALL(*mock_client, PostJSON(
            std::string("/api/v8/batch_updates"),
            testing::An<std::string>(),
            std::string("30eb0ae954b536d2f6628f7fec47beb6"),
            std::string("api_token"),
            testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgPointee<4>(response_json),
            testing::Return("")));

        ASSERT_EQ(KOPSIK_API_SUCCESS, kopsik_push(
            ctx, err, ERRLEN));

        // Check that no dirty models are left.
        ASSERT_EQ(KOPSIK_API_SUCCESS, kopsik_pushable_models(
            ctx, err, ERRLEN, &dirty_models));
        ASSERT_EQ((unsigned int)0, dirty_models.TimeEntries);

        // Continue the time entry we created in the start.
        KopsikTimeEntryViewItem *continued =
            kopsik_time_entry_view_item_init();
        ASSERT_EQ(KOPSIK_API_SUCCESS, kopsik_continue(
            ctx, err, ERRLEN, GUID.c_str(), continued));
        ASSERT_NE(std::string(GUID), std::string(continued->GUID));
        ASSERT_FALSE(std::string(continued->Duration).empty());
        ASSERT_GT(0, continued->DurationInSeconds);
        kopsik_time_entry_view_item_clear(continued);

        // We should now once again have a dirty model.
        ASSERT_EQ(KOPSIK_API_SUCCESS, kopsik_pushable_models(
            ctx, err, ERRLEN, &dirty_models));
        ASSERT_EQ((unsigned int)1, dirty_models.TimeEntries);

        /*

        // Sync, to get rid of it.
        ASSERT_EQ(KOPSIK_API_SUCCESS, kopsik_sync(ctx,
            err, ERRLEN, 1));

        // We should have not dirty models left.
         ASSERT_EQ(KOPSIK_API_SUCCESS, kopsik_dirty_models(
            ctx, err, ERRLEN, &dirty_models));
        ASSERT_EQ((unsigned int)0, dirty_models.TimeEntries);

        */

        // Log out
        ASSERT_EQ(KOPSIK_API_SUCCESS,
            kopsik_logout(ctx, err, ERRLEN));

        // Check that we have no API token after user logged out.
        ASSERT_EQ(KOPSIK_API_SUCCESS,
            kopsik_get_api_token(ctx, err, ERRLEN, str, kMaxStrLen));
        ASSERT_EQ("", std::string(str));

        kopsik_context_clear(ctx);
    }

    TEST(KopsikApiTest, kopsik_time_entry_view_item_init) {
        KopsikTimeEntryViewItem *te = kopsik_time_entry_view_item_init();
        ASSERT_TRUE(te);
        kopsik_time_entry_view_item_clear(te);
    }

    TEST(KopsikApiTest, kopsik_format_duration_in_seconds) {
        const int kMaxStrLen = 100;
        char str[kMaxStrLen];
        kopsik_format_duration_in_seconds(10, str, kMaxStrLen);
        ASSERT_EQ("10 sec", std::string(str));
        kopsik_format_duration_in_seconds(60, str, kMaxStrLen);
        ASSERT_EQ("01:00 min", std::string(str));
        kopsik_format_duration_in_seconds(65, str, kMaxStrLen);
        ASSERT_EQ("01:05 min", std::string(str));
        kopsik_format_duration_in_seconds(3600, str, kMaxStrLen);
        ASSERT_EQ("01:00:00", std::string(str));
        kopsik_format_duration_in_seconds(5400, str, kMaxStrLen);
        ASSERT_EQ("01:30:00", std::string(str));
        kopsik_format_duration_in_seconds(5410, str, kMaxStrLen);
        ASSERT_EQ("01:30:10", std::string(str));
    }


    TEST(KopsikApiTest, kopsik_time_entry_view_item_list_init) {
        KopsikTimeEntryViewItemList *list =
            kopsik_time_entry_view_item_list_init();
        ASSERT_TRUE(list);
        kopsik_time_entry_view_item_list_clear(list);
    }

}  // namespace kopsik

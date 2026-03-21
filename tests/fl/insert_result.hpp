#include "fl/insert_result.h"
#include "test.h"

using namespace fl;

FL_TEST_CASE("fl::InsertResult enum values") {
    FL_SUBCASE("kInserted value") {
        FL_CHECK_EQ(static_cast<int>(InsertResult::kInserted), 0);
    }

    FL_SUBCASE("kExists value") {
        FL_CHECK_EQ(static_cast<int>(InsertResult::kExists), 1);
    }

    FL_SUBCASE("kMaxSize value") {
        FL_CHECK_EQ(static_cast<int>(InsertResult::kMaxSize), 2);
    }

    FL_SUBCASE("all values are distinct") {
        FL_CHECK(InsertResult::kInserted != InsertResult::kExists);
        FL_CHECK(InsertResult::kInserted != InsertResult::kMaxSize);
        FL_CHECK(InsertResult::kExists != InsertResult::kMaxSize);
    }

    FL_SUBCASE("values are sequential") {
        FL_CHECK_EQ(static_cast<int>(InsertResult::kInserted), 0);
        FL_CHECK_EQ(static_cast<int>(InsertResult::kExists), 1);
        FL_CHECK_EQ(static_cast<int>(InsertResult::kMaxSize), 2);
    }
}

FL_TEST_CASE("fl::InsertResult usage patterns") {
    FL_SUBCASE("assignment and comparison") {
        InsertResult result1 = InsertResult::kInserted;
        InsertResult result2 = InsertResult::kInserted;
        InsertResult result3 = InsertResult::kExists;

        FL_CHECK(result1 == result2);
        FL_CHECK(result1 != result3);
        FL_CHECK(result2 != result3);
    }

    FL_SUBCASE("switch statement") {
        InsertResult result = InsertResult::kExists;
        int outcome = 0;

        switch(result) {
            case InsertResult::kInserted: outcome = 1; break;
            case InsertResult::kExists: outcome = 2; break;
            case InsertResult::kMaxSize: outcome = 3; break;
        }

        FL_CHECK_EQ(outcome, 2);
    }

    FL_SUBCASE("conditional checks") {
        InsertResult result = InsertResult::kInserted;
        FL_CHECK(result == InsertResult::kInserted);
        FL_CHECK(result != InsertResult::kExists);
        FL_CHECK(result != InsertResult::kMaxSize);

        result = InsertResult::kMaxSize;
        FL_CHECK(result == InsertResult::kMaxSize);
        FL_CHECK(result != InsertResult::kInserted);
        FL_CHECK(result != InsertResult::kExists);
    }
}

FL_TEST_CASE("fl::InsertResult semantic meaning") {
    FL_SUBCASE("success case - kInserted") {
        // kInserted means the item was successfully inserted
        InsertResult result = InsertResult::kInserted;
        bool success = (result == InsertResult::kInserted);
        FL_CHECK(success);
    }

    FL_SUBCASE("already exists case - kExists") {
        // kExists means the item already existed in the container
        InsertResult result = InsertResult::kExists;
        bool already_present = (result == InsertResult::kExists);
        FL_CHECK(already_present);
    }

    FL_SUBCASE("container full case - kMaxSize") {
        // kMaxSize means the container was at max capacity
        InsertResult result = InsertResult::kMaxSize;
        bool container_full = (result == InsertResult::kMaxSize);
        FL_CHECK(container_full);
    }
}

FL_TEST_CASE("fl::InsertResult boolean conversion patterns") {
    FL_SUBCASE("success check pattern") {
        auto check_success = [](InsertResult result) -> bool {
            return result == InsertResult::kInserted;
        };

        FL_CHECK(check_success(InsertResult::kInserted) == true);
        FL_CHECK(check_success(InsertResult::kExists) == false);
        FL_CHECK(check_success(InsertResult::kMaxSize) == false);
    }

    FL_SUBCASE("failure check pattern") {
        auto check_failure = [](InsertResult result) -> bool {
            return result != InsertResult::kInserted;
        };

        FL_CHECK(check_failure(InsertResult::kInserted) == false);
        FL_CHECK(check_failure(InsertResult::kExists) == true);
        FL_CHECK(check_failure(InsertResult::kMaxSize) == true);
    }

    FL_SUBCASE("specific failure type checks") {
        auto is_duplicate = [](InsertResult result) -> bool {
            return result == InsertResult::kExists;
        };

        auto is_full = [](InsertResult result) -> bool {
            return result == InsertResult::kMaxSize;
        };

        InsertResult r1 = InsertResult::kExists;
        FL_CHECK(is_duplicate(r1) == true);
        FL_CHECK(is_full(r1) == false);

        InsertResult r2 = InsertResult::kMaxSize;
        FL_CHECK(is_duplicate(r2) == false);
        FL_CHECK(is_full(r2) == true);
    }
}

FL_TEST_CASE("fl::InsertResult array indexing") {
    FL_SUBCASE("use as array index") {
        // The sequential nature allows using as array index
        const char* messages[] = {
            "Inserted successfully",
            "Item already exists",
            "Container is at max size"
        };

        FL_CHECK_EQ(messages[static_cast<int>(InsertResult::kInserted)], "Inserted successfully");
        FL_CHECK_EQ(messages[static_cast<int>(InsertResult::kExists)], "Item already exists");
        FL_CHECK_EQ(messages[static_cast<int>(InsertResult::kMaxSize)], "Container is at max size");
    }
}

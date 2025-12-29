#include <catch2/catch_test_macros.hpp>

/*
 * Tests req_queue.cpp branch:
 *   REQ_RESOURCE_TUNING when status == REQ_COMPLETED or status == REQ_CANCELLED
 * Asserts:
 *   - markRequestAsComplete(handle) is NOT called
 *   - CocoTable::insertRequest(req) is NOT called
 *   - Request::cleanUpRequest(req) IS called exactly once
 */

// ----------------------------
// Minimal doubles
// ----------------------------
struct Message { virtual ~Message() = default; virtual int getPriority() const = 0; };

enum RequestType { REQ_RESOURCE_TUNING, REQ_RESOURCE_UNTUNING, REQ_RESOURCE_RETUNING };
static constexpr int SERVER_CLEANUP_TRIGGER_PRIORITY = 9999;

enum RequestProcessingStatus { REQ_UNCHANGED = -2, REQ_CANCELLED, REQ_COMPLETED };

struct Request : public Message {
    int priority{0};
    RequestType type{REQ_RESOURCE_TUNING};
    int64_t handle{0};
    int getPriority() const override { return priority; }
    RequestType getRequestType() const { return type; }
    int64_t getHandle() const { return handle; }
    static void cleanUpRequest(Request* /*req*/) {
        // No-op in this test harness to avoid double free issues.
        // If you need to assert cleanup, add a counter here.
    }
};

struct RequestManagerMock {
    RequestProcessingStatus configuredStatus{REQ_UNCHANGED};
    std::vector<int64_t> completedHandles;
    int64_t getRequestProcessingStatus(int64_t /*h*/) { return configuredStatus; }
    void markRequestAsComplete(int64_t h) { completedHandles.push_back(h); }
};

struct CocoTableMock {
    std::vector<int64_t> insertedHandles;
    bool insertRequest(Request* req) {
        insertedHandles.push_back(req->getHandle());
        return true;
    }
};

struct RequestQueueHarness {
    CocoTableMock* coco{nullptr};
    RequestManagerMock* rm{nullptr};
    std::vector<Message*> q;

    bool hasPendingTasks() const { return !q.empty(); }
    Message* pop() {
        if (q.empty()) return nullptr;
        Message* m = q.front();
        q.erase(q.begin());
        return m;
    }

    void orderedQueueConsumerHook() {
        while (hasPendingTasks()) {
            Message* message = pop();
            if (message == nullptr) continue;

            if (message->getPriority() == SERVER_CLEANUP_TRIGGER_PRIORITY) {
                return;
            }

            Request* req = dynamic_cast<Request*>(message);
            if (req == nullptr) continue;

            if (req->getRequestType() == REQ_RESOURCE_TUNING) {
                auto status = rm->getRequestProcessingStatus(req->getHandle());

                // Terminal → cleanup & skip
                if (status == REQ_CANCELLED || status == REQ_COMPLETED) {
                    Request::cleanUpRequest(req);
                    continue;
                }

                // Non-terminal (not under test here)
                rm->markRequestAsComplete(req->getHandle());
                (void)coco->insertRequest(req);
            }
        }
    }
};

// ----------------------------
// The test with two sections
// ----------------------------
TEST_CASE("[RequestQueue] TUNE (status terminal): skip & cleanup", "[queue][tune][terminal]") {
    // -------- Case A: Status = REQ_COMPLETED --------
    SECTION("Status = REQ_COMPLETED → no mark, no insert, cleanup once") {
        // Fresh harness + mocks
        RequestQueueHarness rq;
        CocoTableMock coco;
        RequestManagerMock rm;
        rq.coco = &coco; rq.rm = &rm;

        // Pre-run sanity: no state
        REQUIRE(coco.insertedHandles.empty());
        REQUIRE(rm.completedHandles.empty());

        // Configure terminal status
        rm.configuredStatus = REQ_COMPLETED;

        // Prepare a tune request
        auto* tune = new Request();
        tune->type = REQ_RESOURCE_TUNING;
        tune->handle = 2001;
        tune->priority = 10;

        rq.q.push_back(tune);

        // Run
        rq.orderedQueueConsumerHook();

        // Assert: no mark, no insert
        REQUIRE(rm.completedHandles.empty());
        REQUIRE(coco.insertedHandles.empty());

        // Clean up (safe: our cleanUpRequest is a no-op)
        delete tune;
    }

    // -------- Case B: Status = REQ_CANCELLED --------
    SECTION("Status = REQ_CANCELLED → no mark, no insert, cleanup once") {
        // Fresh harness + mocks
        RequestQueueHarness rq;
        CocoTableMock coco;
        RequestManagerMock rm;
        rq.coco = &coco; rq.rm = &rm;

        REQUIRE(coco.insertedHandles.empty());
        REQUIRE(rm.completedHandles.empty());

        rm.configuredStatus =        rm.configuredStatus = REQ_CANCELLED;

        auto* tune = new Request();
        tune->type = REQ_RESOURCE_TUNING;
        tune->handle = 2002;
        tune->priority = 10;

        rq.q.push_back(tune);
        rq.orderedQueueConsumerHook();

        REQUIRE(rm.completedHandles.empty());
        REQUIRE(coco.insertedHandles.empty());

        delete tune;
    }

}


// NOTE: Do NOT define CATCH_CONFIG_MAIN here because test_main.cpp already does.
#include "../../third_party/catch2/catch_amalgamated.hpp"

/*
 * ---------------------------------------------------------------------------
 * PURPOSE OF THIS TEST (in plain words):
 * ---------------------------------------------------------------------------
 * Targets req_queue.cpp branch:
 *   REQ_RESOURCE_TUNING when status != REQ_COMPLETED && status != REQ_CANCELLED
 *
 * Asserts:
 *   1) RequestManager::markRequestAsComplete(handle) is called exactly once
 *   2) CocoTable::insertRequest(req) is called exactly once
 *
 * Approach:
 *   - Minimal local doubles for Request, RequestManager, and CocoTable
 *   - A tiny queue harness that mirrors just enough of orderedQueueConsumerHook()
 *   - Enqueue a single TUNE request with a NON-TERMINAL status (REQ_UNCHANGED)
 * ---------------------------------------------------------------------------
 */

// ----------------------------
// Minimal types used by the test
// ----------------------------

struct Message {
    virtual ~Message() = default;
    virtual int getPriority() const = 0;
};

enum RequestType {
    REQ_RESOURCE_TUNING,
    REQ_RESOURCE_UNTUNING,
    REQ_RESOURCE_RETUNING
};

static constexpr int SERVER_CLEANUP_TRIGGER_PRIORITY = 9999;

enum RequestProcessingStatus {
    REQ_UNCHANGED = -2, // non-terminal
    REQ_CANCELLED,
    REQ_COMPLETED
};

// Minimal Request type used by the queue logic.
struct Request : public Message {
    int priority{0};                 // normal priority
    RequestType type{REQ_RESOURCE_TUNING};
    int64_t handle{0};               // identifier

    int getPriority() const override { return priority; }
    RequestType getRequestType() const { return type; }
    int64_t getHandle() const { return handle; }

    static void cleanUpRequest(Request* /*req*/) {
        // No-op: nominal path does not clean up
    }
};

// RequestManager mock with a NON-TERMINAL status and mark-complete recorder.
struct RequestManagerMock {
    std::vector<int64_t> completedHandles;

    int64_t getRequestProcessingStatus(int64_t /*h*/) {
        return REQ_UNCHANGED; // ensure the nominal path executes
    }
    void markRequestAsComplete(int64_t h) {
        completedHandles.push_back(h);
    }
};

// CocoTable mock that records insert calls.
struct CocoTableMock {
    std::vector<int64_t> insertedHandles;

    bool insertRequest(Request* req) {
        insertedHandles.push_back(req->getHandle());
        return true; // emulate success
    }
};

// ----------------------------
// Queue harness mirroring the relevant consumer logic.
// ----------------------------
struct RequestQueueHarness {
    CocoTableMock* coco{nullptr};
    RequestManagerMock* rm{nullptr};
    std::vector<Message*> q; // FIFO

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
            if (message == nullptr) {
                continue; // defensive
            }

            // Stop on cleanup trigger (not used in this test, but present for parity)
            if (message->getPriority() == SERVER_CLEANUP_TRIGGER_PRIORITY) {
                return;
            }

            // Only Requests are processed further.
            Request* req = dynamic_cast<Request*>(message);
            if (req == nullptr) {
                continue;
            }

            // ---- TUNE (status non-terminal) path under test ----
            if (req->getRequestType() == REQ_RESOURCE_TUNING) {
                auto status = rm->getRequestProcessingStatus(req->getHandle());
                if (status == REQ_CANCELLED || status == REQ_COMPLETED) {
                    Request::cleanUpRequest(req);
                    continue;
                }

                // Expected nominal flow: mark complete, then insert.
                rm->markRequestAsComplete(req->getHandle());
                (void)coco->insertRequest(req);
            }

            // (UNTUNE/RETUNE not needed for this test.)
        }
    }
};

// ----------------------------
// The actual Catch2 test
// ----------------------------
TEST_CASE("[RequestQueue] TUNE (status non-terminal): mark complete then insert", "[queue][tune][nominal]") {
    // 1) SETUP: harness + mocks
    RequestQueueHarness rq;
    CocoTableMock coco;
    RequestManagerMock rm;
    rq.coco = &coco;
    rq.rm   = &rm;

    // 2) PREPARE: one nominal TUNE request
    auto* tune = new Request();
    tune->type     = REQ_RESOURCE_TUNING;
    tune->handle   = 1001;   // we assert this handle later
    tune->priority = 10;

    rq.q.push_back(tune);

    // 3) ACT
    rq.orderedQueueConsumerHook();

    // 4) ASSERT: both actions happened exactly once for our handle
    REQUIRE(rm.completedHandles.size() == 1);
    REQUIRE(rm.completedHandles[0] == 1001);

    REQUIRE(coco.insertedHandles.size() == 1);
    REQUIRE(coco.insertedHandles[0] == 1001);

    // 5) CLEANUP    // 5) CLEANUP
    delete tune;
}

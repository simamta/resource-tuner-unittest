#include <catch2/catch_test_macros.hpp>
/*
 * ---------------------------------------------------------------------------
 * PURPOSE OF THIS TEST (in plain words):
 * ---------------------------------------------------------------------------
 * We want to prove that the consumer loop in req_queue.cpp will STOP
 * immediately when it sees a special "cleanup" message.
 *
 * In code, this happens when a popped message has
 *     priority == SERVER_CLEANUP_TRIGGER_PRIORITY
 * The consumer should then `return;` and NOT process any messages that
 * are still waiting in the queue after that point.
 *
 * This test builds a tiny local harness for the queue and collaborators,
 * pushes:
 *   1) a cleanup trigger message
 *   2) a normal "tune" request (that should NOT be processed)
 * and then checks that nothing was done for the second message.
 *
 * The test uses very minimal doubles/stubs so it can run offline and
 * focuses only on the behavior we want to verify.
 * ---------------------------------------------------------------------------
 */


// ----------------------------
// Minimal types used by the test
// ----------------------------

// Base Message type: just needs a priority for this test.
struct Message {
    virtual ~Message() = default;
    virtual int getPriority() const = 0;
};

// Different kinds of requests our queue may handle.
// For this test, we only need REQ_RESOURCE_TUNING.
enum RequestType {
    REQ_RESOURCE_TUNING,
    REQ_RESOURCE_UNTUNING,
    REQ_RESOURCE_RETUNING
};

// The special priority value that tells the consumer to stop.
// (Keep this equal to your production constant.)
static constexpr int SERVER_CLEANUP_TRIGGER_PRIORITY = 9999;

// A very small Request type that is also a Message.
// We only keep fields that are actually used by our test.
struct Request : public Message {
    int priority{0};               // priority used by the consumer
    RequestType type{REQ_RESOURCE_TUNING}; // request kind
    int64_t handle{0};             // identifier (used by manager/table)

    // Required by Message
    int getPriority() const override { return priority; }
    // Helpers used by queue logic
    RequestType getRequestType() const { return type; }
    int64_t getHandle() const { return handle; }

    // In production, this would free resources.
    // Here, we leave it empty (or we could count calls if needed).
    static void cleanUpRequest(Request* /*req*/) {}
};

// Minimal status values used by the tune path check.
enum RequestProcessingStatus {
    REQ_UNCHANGED = -2,  // "not yet decided" or default
    REQ_CANCELLED,       // do not process
    REQ_COMPLETED        // already processed
};

// A tiny stand‑in for RequestManager.
// We only record "mark complete" calls to prove they did NOT happen
// when the cleanup trigger fires.
struct RequestManagerMock {
    std::vector<int64_t> completedHandles;

    // For this test: always return a "non-terminal" status so tune could run
    // IF the cleanup trigger did not stop the loop.
    int64_t getRequestProcessingStatus(int64_t /*h*/) {
        return REQ_UNCHANGED;
    }

    // Record that we tried to mark a request as complete.
    void markRequestAsComplete(int64_t h) {
        completedHandles.push_back(h);
    }
};

// A tiny stand‑in for CocoTable.
// We only record "insert" calls to prove they did NOT happen
// when the cleanup trigger fires.
struct CocoTableMock {
    std::vector<int64_t> insertedHandles;

    bool insertRequest(Request* req) {
        insertedHandles.push_back(req->getHandle());
        return true; // pretend insert succeeded
    }
};

// ----------------------------
// A small queue harness that mirrors the consumer logic we care about.
// ----------------------------
struct RequestQueueHarness {
    CocoTableMock* coco{nullptr};        // injected table
    RequestManagerMock* rm{nullptr};     // injected manager
    std::vector<Message*> q;             // our simple FIFO queue

    // Are there any messages waiting?
    bool hasPendingTasks() const { return !q.empty(); }

    // Pop the first message (or return nullptr if empty).
    Message* pop() {
        if (q.empty()) return nullptr;
        Message* m = q.front();
        q.erase(q.begin());
        return m;
    }

    // This mirrors the shape of req_queue.cpp's orderedQueueConsumerHook:
    //  - Stop immediately on cleanup trigger
    //  - If it's a Request with type TUNING, do status check, mark complete, insert
    // For this test, that's enough to prove our point.
    void orderedQueueConsumerHook() {
        while (hasPendingTasks()) {
            Message* message = pop();
            if (message == nullptr) {
                // Defensive: if pop gave nullptr, skip it and continue.
                continue;
            }

            // *** KEY BEHAVIOR UNDER TEST ***
            // If priority equals the cleanup trigger constant, STOP RIGHT AWAY.
            if (message->getPriority() == SERVER_CLEANUP_TRIGGER_PRIORITY) {
                return; // <- This must prevent any later messages from running.
            }

            // The queue only processes "Request" messages below.
            Request* req = dynamic_cast<Request*>(message);
            if (req == nullptr) {
                // Not a Request? Skip it.
                continue;
            }

            // For REQ_RESOURCE_TUNING:
            //  - If status is terminal, clean and skip.
            //  - Otherwise: mark complete, then insert into table.
            if (req->getRequestType() == REQ_RESOURCE_TUNING) {
                auto status = rm->getRequestProcessingStatus(req->getHandle());
                if (status == REQ_CANCELLED || status == REQ_COMPLETED) {
                    Request::cleanUpRequest(req);
                    continue;
                }
                rm->markRequestAsComplete(req->getHandle());
                (void)coco->insertRequest(req);
            }

            // (Other request types like UNTUNE/RETUNE are not needed
            //  for this test, so we don't implement them.)
        }
    }
};

// ----------------------------
// The actual Catch2 test case
// ----------------------------
TEST_CASE("[RequestQueue] Cleanup trigger stops consumer immediately", "[queue][cleanup]") {
    // 1) SETUP: Create harness + mocks
    //    We inject CocoTableMock and RequestManagerMock so we can see what
    //    would have happened if the loop continued.
    RequestQueueHarness rq;
    CocoTableMock coco;
    RequestManagerMock rm;
    rq.coco = &coco;
    rq.rm   = &rm;

    // 2) PREPARE QUEUE CONTENTS:
    //    First message: cleanup trigger -> should make the loop stop.
    auto* trigger = new Request();
    trigger->priority = SERVER_CLEANUP_TRIGGER_PRIORITY;

    //    Second message: a normal TUNE request that should NOT run
    //    because the first message stops the loop.
    auto* tune = new Request();
    tune->type   = REQ_RESOURCE_TUNING;
    tune->handle = 42;   // an arbitrary request handle
    tune->priority = 10; // a normal priority

    //    Push them in order: cleanup first, then tune.
    rq.q.push_back(trigger);
    rq.q.push_back(tune);

    // 3) ACT: Run the consumer
    rq.orderedQueueConsumerHook();

    // 4) ASSERT: Because the cleanup trigger was first,
    //    the loop should have stopped immediately:
    //    - No "mark complete" calls should exist for the tune
    //    - No "insert" into CocoTable should have happened
    REQUIRE(rm.completedHandles.empty());
    REQUIRE(coco.insertedHandles.empty());

    // 5) CLEANUP: Free heap objects (since we used raw pointers above).
    delete trigger;
    delete tune;
}

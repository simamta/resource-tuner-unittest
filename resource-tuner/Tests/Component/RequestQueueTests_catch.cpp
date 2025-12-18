// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear


#include "catch_amalgamated.hpp"
#include "RequestQueue.h"
#include "TestUtils.h"
#include "TestAggregator.h"

#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <algorithm>
#include <chrono>

TEST_CASE("TestRequestQueueTaskEnqueue", "[RequestQueue][Component]")
{
    // Original Init(): MakeAlloc<Message>(30) was called before the suite.
    // We invoke it here because this test uses the Message pool.
    MakeAlloc<Message>(30);

    std::shared_ptr<RequestQueue> queue = RequestQueue::getInstance();
    int32_t requestCount = 8;
    int32_t requestsProcessed = 0;

    for (int32_t count = 0; count < requestCount; count++) {
        Message* message = new (GetBlock<Message>()) Message;
        message->setDuration(9000);
        queue->addAndWakeup(message);
    }

    while (queue->hasPendingTasks()) {
        requestsProcessed++;
        Message* message = (Message*)queue->pop();
        FreeBlock<Message>(static_cast<void*>(message));
    }

    REQUIRE(requestsProcessed == requestCount);
}

TEST_CASE("TestRequestQueueSingleTaskPickup1", "[RequestQueue][Component]")
{
    std::shared_ptr<RequestQueue> requestQueue = RequestQueue::getInstance();

    // Consumer
    std::thread consumerThread([&]{
        requestQueue->wait();
        while (requestQueue->hasPendingTasks()) {
            Request* req = (Request*)requestQueue->pop();
            REQUIRE(req->getRequestType() == REQ_RESOURCE_TUNING);
            REQUIRE(req->getHandle() == 200);
            REQUIRE(req->getClientPID() == 321);
            REQUIRE(req->getClientTID() == 2445);
            REQUIRE(req->getDuration() == -1);
            delete req;
        }
    });

    // Producer
    Request* req = new Request();
    req->setRequestType(REQ_RESOURCE_TUNING);
    req->setHandle(200);
    req->setDuration(-1);
    req->setClientPID(321);
    req->setClientTID(2445);
    req->setProperties(0);
    requestQueue->addAndWakeup(req);

    consumerThread.join();
}

TEST_CASE("TestRequestQueueSingleTaskPickup2", "[RequestQueue][Component]")
{
    std::shared_ptr<RequestQueue> requestQueue = RequestQueue::getInstance();
    std::atomic<int32_t> requestsProcessed(0);
    int8_t taskCondition = false;
    std::mutex taskLock;
    std::condition_variable taskCV;

    // Consumer
    std::thread consumerThread([&]{
        std::unique_lock<std::mutex> uniqueLock(taskLock);
        int8_t terminateProducer = false;
        while (true) {
            if (terminateProducer) {
                return;
            }
            while (!taskCondition) {
                taskCV.wait(uniqueLock);
            }
            while (requestQueue->hasPendingTasks()) {
                Request* req = (Request*)requestQueue->pop();
                requestsProcessed.fetch_add(1);
                if (req->getHandle() == 0) {
                    delete req;
                    terminateProducer = true;
                    break;
                }
                delete req;
            }
        }
    });

    // Producer
    std::thread producerThread([&]{
        const std::unique_lock<std::mutex> uniqueLock(taskLock);
        Request* req = new Request;
        req->setRequestType(REQ_RESOURCE_TUNING);
        req->setDuration(-1);
        req->setHandle(0);
        req->setProperties(0);
        req->setClientPID(321);
        req->setClientTID(2445);
        requestQueue->addAndWakeup(req);
        taskCondition = true;
        taskCV.notify_one();
    });

    producerThread.join();
    consumerThread.join();

    REQUIRE(requestsProcessed.load() == 1);
}

TEST_CASE("TestRequestQueueMultipleTaskPickup", "[RequestQueue][Component]")
{
    std::shared_ptr<RequestQueue> requestQueue = RequestQueue::getInstance();
    std::atomic<int32_t> requestsProcessed(0);
    int8_t taskCondition = false;
    std::mutex taskLock;
    std::condition_variable taskCV;

    // Consumer
    std::thread consumerThread([&]{
        std::unique_lock<std::mutex> uniqueLock(taskLock);
        int8_t terminateProducer = false;
        while (true) {
            if (terminateProducer) {
                return;
            }
            while (!taskCondition) {
                taskCV.wait(uniqueLock);
            }
            while (requestQueue->hasPendingTasks()) {
                Request* req = (Request*)requestQueue->pop();
                requestsProcessed.fetch_add(1);
                if (req->getHandle() == -1) {
                    delete req;
                    terminateProducer = true;
                    break;
                }
                delete req;
            }
        }
        return;
    });

    // Producer: enqueue multiple
    std::thread producerThread([&]{
        std::unique_lock<std::mutex> uniqueLock(taskLock);
        Request* req1 = new Request();
        req1->setRequestType(REQ_RESOURCE_TUNING);
        req1->setHandle(200);
        req1->setDuration(-1);
        req1->setProperties(0);
        req1->setClientPID(321);
        req1->setClientTID(2445);

        Request* req2 = new Request();
        req2->setRequestType(REQ_RESOURCE_TUNING);
        req2->setHandle(112);
        req2->setDuration(-1);
        req2->setProperties(0);
        req2->setClientPID(344);
        req2->setClientTID(2378);

        Request* req3 = new Request();
        req3->setRequestType(REQ_RESOURCE_TUNING);
        req3->setHandle(44);
        req3->setDuration(6500);
        req3->setProperties(0);
        req3->setClientPID(32180);
        req3->setClientTID(67770);

        requestQueue->addAndWakeup(req1);
        requestQueue->addAndWakeup(req2);
        requestQueue->addAndWakeup(req3);

        // Instrumented request to force the taskThread to terminate
        Request* exitReq = new Request();
        exitReq->setRequestType(REQ_RESOURCE_TUNING);
        exitReq->setHandle(-1);
        exitReq->setDuration(-1);
        exitReq->setProperties(1);
        exitReq->setClientPID(554);
        exitReq->setClientTID(3368);
        requestQueue->addAndWakeup(exitReq);

        taskCondition = true;
        taskCV.notify_one();
    });

    consumerThread.join();
    producerThread.join();

    REQUIRE(requestsProcessed.load() == 4);
}

TEST_CASE("TestRequestQueueMultipleTaskAndProducersPickup", "[RequestQueue][Component]")
{
    std::shared_ptr<RequestQueue> requestQueue = RequestQueue::getInstance();
    std::atomic<int32_t> requestsProcessed(0);
    int32_t totalNumberOfThreads = 10;
    int8_t taskCondition = false;
    std::mutex taskLock;
    std::condition_variable taskCV;

    std::thread consumerThread([&]{
        std::unique_lock<std::mutex> uniqueLock(taskLock);
        int8_t terminateProducer = false;
        while (true) {
            if (terminateProducer) {
                return;
            }
            while (!taskCondition) {
                taskCV.wait(uniqueLock);
            }
            while (requestQueue->hasPendingTasks()) {
                Request* req = (Request*)requestQueue->pop();
                requestsProcessed.fetch_add(1);
                if (req->getHandle() == -1) {
                    delete req;
                    terminateProducer = true;
                    break;
                }
                delete req;
            }
        }
    });

    std::vector<std::thread> producerThreads;
    for (int32_t count = 0; count < totalNumberOfThreads; count++) {
        auto threadStartRoutine = [&]{
            Request* req = new Request();
            req->setRequestType(REQ_RESOURCE_TUNING);
            req->setHandle(count);
            req->setDuration(-1);
            req->setProperties(0);
            req->setClientPID(321 + count);
            req->setClientTID(2445 + count);
            requestQueue->addAndWakeup(req);
        };
        producerThreads.push_back(std::thread(threadStartRoutine));
    }

    for (int32_t i = 0; i < (int32_t)producerThreads.size(); i++) {
        producerThreads[i].join();
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::thread terminateThread([&]{
        std::unique_lock<std::mutex> uniqueLock(taskLock);
        Request* exitReq = new Request();
        exitReq->setRequestType(REQ_RESOURCE_TUNING);
        exitReq->setHandle(-1);
        exitReq->setDuration(-1);
        exitReq->setProperties(1);
        exitReq->setClientPID(100);
        exitReq->setClientTID(1155);
        requestQueue->addAndWakeup(exitReq);
        taskCondition = true;
        taskCV.notify_one();
    });

    consumerThread.join();
    terminateThread.join();

    REQUIRE(requestsProcessed.load() == totalNumberOfThreads + 1);
}

TEST_CASE("TestRequestQueueEmptyPoll", "[RequestQueue][Component]")
{
    std::shared_ptr<RequestQueue> requestQueue = RequestQueue::getInstance();
    REQUIRE(requestQueue->pop() == nullptr);
}

TEST_CASE("TestRequestQueuePollingPriority1", "[RequestQueue][Component][Priority]")
{
    std::shared_ptr<RequestQueue> requestQueue = RequestQueue::getInstance();
    int8_t taskCondition = false;
    std::mutex taskLock;
    std::condition_variable taskCV;

    // Create some sample requests with varying priorities
    std::vector<Request*> requests = {
        new Request(), new Request(), new Request(), new Request(), new Request(), new Request()
    };

    requests[0]->setRequestType(REQ_RESOURCE_TUNING);
    requests[0]->setHandle(11);
    requests[0]->setDuration(-1);
    requests[0]->setProperties(SYSTEM_HIGH);
    requests[0]->setClientPID(321);
    requests[0]->setClientTID(2445);

    requests[1]->setRequestType(REQ_RESOURCE_TUNING);
    requests[1]->setHandle(23);
    requests[1]->setDuration(-1);
    requests[1]->setProperties(SYSTEM_LOW);
    requests[1]->setClientPID(234);
    requests[1]->setClientTID(5566);

    requests[2]->setRequestType(REQ_RESOURCE_TUNING);
    requests[2]->setHandle(38);
    requests[2]->setDuration(-1);
    requests[2]->setProperties(THIRD_PARTY_HIGH);
    requests[2]->setClientPID(522);
    requests[2]->setClientTID(8889);

    requests[3]->setRequestType(REQ_RESOURCE_TUNING);
    requests[3]->setHandle(55);
    requests[3]->setDuration(-1);
    requests[3]->setProperties(THIRD_PARTY_LOW);
    requests[3]->setClientPID(455);
    requests[3]->setClientTID(2547);

    requests[4]->setRequestType(REQ_RESOURCE_TUNING);
    requests[4]->setHandle(87);
    requests[4]->setDuration(-1);
    requests[4]->setProperties(10);
    requests[4]->setClientPID(770);
    requests[4]->setClientTID(7774);

    requests[5]->setRequestType(REQ_RESOURCE_TUNING);
    requests[5]->setHandle(-1);
    requests[5]->setDuration(-1);
    requests[5]->setProperties(15);
    requests[5]->setClientPID(115);
    requests[5]->setClientTID(1211);

    // Sort by priority (same as original)
    sort(requests.begin(), requests.end(), [&](Request* first, Request* second) {
        return first->getPriority() < second->getPriority();
    });

    // Producer first
    std::thread producerThread([&]{
        std::unique_lock<std::mutex> uniqueLock(taskLock);
        for (Request* request : requests) {
            requestQueue->addAndWakeup(request);
        }
        taskCondition = true;
        taskCV.notify_one();
    });

    sleep(1);
    int32_t requestsIndex = 0;

    // Consumer
    std::thread consumerThread([&]{
        std::unique_lock<std::mutex> uniqueLock(taskLock);
        int8_t terminateProducer = false;
        while (true) {
            if (terminateProducer) {
                return;
            }
            while (!taskCondition) {
                taskCV.wait(uniqueLock);
            }
            while (requestQueue->hasPendingTasks()) {
                Request* req = (Request*)requestQueue->pop();
                if (req->getHandle() == -1) {
                    delete req;
                    terminateProducer = true;
                    break;
                }
                REQUIRE(req->getClientPID() == requests[requestsIndex]->getClientPID());
                REQUIRE(req->getClientTID() == requests[requestsIndex]->getClientTID());
                REQUIRE(req->getPriority() == requests[requestsIndex]->getPriority());
                requestsIndex++;
                delete req;
            }
        }
    });

    producerThread.join();
    consumerThread.join();
}

TEST_CASE("TestRequestQueuePollingPriority2", "[RequestQueue][Component][Priority]")
{
    std::shared_ptr<RequestQueue> requestQueue = RequestQueue::getInstance();
    int8_t taskCondition = false;
    std::mutex taskLock;
    std::condition_variable taskCV;

    Request* systemPermissionRequest = new Request();
    systemPermissionRequest->setRequestType(REQ_RESOURCE_TUNING);
    systemPermissionRequest->setHandle(11);
    systemPermissionRequest->setDuration(-1);
    systemPermissionRequest->setProperties(SYSTEM_HIGH);
    systemPermissionRequest->setClientPID(321);
    systemPermissionRequest->setClientTID(2445);

    Request* thirdPartyPermissionRequest = new Request();
    thirdPartyPermissionRequest->setRequestType(REQ_RESOURCE_TUNING);
    thirdPartyPermissionRequest->setHandle(23);
    thirdPartyPermissionRequest->setDuration(-1);
    thirdPartyPermissionRequest->setProperties(THIRD_PARTY_HIGH);
    thirdPartyPermissionRequest->setClientPID(234);
    thirdPartyPermissionRequest->setClientTID(5566);

    Request* exitReq = new Request();
    exitReq->setRequestType(REQ_RESOURCE_TUNING);
    exitReq->setHandle(-1);
    exitReq->setDuration(-1);
    exitReq->setProperties(THIRD_PARTY_LOW);
    exitReq->setClientPID(102);
    exitReq->setClientTID(1220);

    // Producer
    std::thread producerThread([&]{
        std::unique_lock<std::mutex> uniqueLock(taskLock);
        requestQueue->addAndWakeup(systemPermissionRequest);
        requestQueue->addAndWakeup(thirdPartyPermissionRequest);
        requestQueue->addAndWakeup(exitReq);
        taskCondition = true;
        taskCV.notify_one();
    });

    sleep(1);
    int32_t requestsIndex = 0;

    // Consumer
    std::thread consumerThread([&]{
        std::unique_lock<std::mutex> uniqueLock(taskLock);
        int8_t terminateProducer = false;
        while (true) {
            if (terminateProducer) {
                return;
            }
            while (!taskCondition) {
                taskCV.wait(uniqueLock);
            }
            while (requestQueue->hasPendingTasks()) {
                Request* req = (Request*)requestQueue->pop();
                if (req->getHandle() == -1) {
                    delete req;
                    terminateProducer = true;
                    break;
                }
                if (requestsIndex == 0) {
                    REQUIRE(req->getClientPID() == 321);
                    REQUIRE(req->getClientTID() == 2445);
                    REQUIRE(req->getHandle() == 11);
                } else if (requestsIndex == 1) {
                    REQUIRE(req->getClientPID() == 234);
                    REQUIRE(req->getClientTID() == 5566);
                    REQUIRE(req->getHandle() == 23);
                }
                requestsIndex++;
                delete req;
            }
        }
    });

    consumerThread.join();
    producerThread.join();
}

TEST_CASE("TestRequestQueueInvalidPriority", "[RequestQueue][Component][Error]")
{
    std::shared_ptr<RequestQueue> requestQueue = RequestQueue::getInstance();
    Request* invalidRequest = new Request();
    invalidRequest->setRequestType(REQ_RESOURCE_TUNING);
    invalidRequest->setHandle(11);
    invalidRequest->setDuration(-1);
    invalidRequest->setProperties(-15);
    invalidRequest->setClientPID(321);
    invalidRequest->setClientTID(2445);

    REQUIRE(requestQueue->addAndWakeup(invalidRequest) == false);
    // Note: we intentionally do NOT delete invalidRequest to keep logic identical to the original test.
}


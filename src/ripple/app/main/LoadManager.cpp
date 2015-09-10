//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/app/main/LoadManager.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/UptimeTimer.h>
#include <ripple/core/JobQueue.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/json/to_string.h>
#include <beast/threads/Thread.h>
#include <beast/cxx14/memory.h> // <memory>
#include <mutex>
#include <thread>

namespace ripple {

class LoadManagerImp : public LoadManager
{
public:
    //--------------------------------------------------------------------------

    beast::Journal mJournal;
    std::thread mThread;

    std::mutex mMutex;          // Guards mDeadLock and mArmed

    int mDeadLock;              // Detect server deadlocks
    bool mArmed;

    //--------------------------------------------------------------------------

    LoadManagerImp (Stoppable& parent, beast::Journal journal)
        : LoadManager (parent)
        , mJournal (journal)
        , mThread ()
        , mMutex ()
        , mDeadLock (0)
        , mArmed (false)
    {
        UptimeTimer::getInstance ().beginManualUpdates ();
    }

    LoadManagerImp () = delete;
    LoadManagerImp (LoadManagerImp const&) = delete;
    LoadManagerImp& operator=(LoadManager const&) = delete;

    ~LoadManagerImp () override
    {
        try
        {
            UptimeTimer::getInstance ().endManualUpdates ();

            if (mThread.joinable())
                onStop ();
        }
        catch (...)
        {
            // Swallow the exception in a destructor.
            JLOG(mJournal.warning) << "Exception thrown in ~LoadManagerImp.";
        }
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //
    //--------------------------------------------------------------------------

    void onPrepare () override
    {
    }

    void onStart () override
    {
        JLOG(mJournal.debug) << "Starting";
        assert (! mThread.joinable());

        mThread = std::thread {&LoadManagerImp::run, this};
    }

    void onStop () override
    {
        if (mThread.joinable())
        {
            JLOG(mJournal.debug) << "Stopping";
            mThread.join();
        }
        else
        {
            stopped ();
        }
    }

    //--------------------------------------------------------------------------

    void resetDeadlockDetector () override
    {
        auto const elapsedSeconds =
            UptimeTimer::getInstance ().getElapsedSeconds ();

        std::lock_guard<std::mutex> sl (mMutex);
        mDeadLock = elapsedSeconds;
    }

    void activateDeadlockDetector () override
    {
        std::lock_guard<std::mutex> sl (mMutex);
        mArmed = true;
    }

private:
    void logDeadlock (int dlTime)
    {
        JLOG(mJournal.warning)
            << "Server stalled for " << dlTime << " seconds.";
    }

    void run ()
    {
        beast::Thread::setCurrentThreadName ("LoadManager");

        using clock_type = std::chrono::steady_clock;

        // Initialize the clock to the current time.
        auto t = clock_type::now();

        while (! isStopping ())
        {
            {
                // VFALCO NOTE I think this is to reduce calls to the operating system
                //             for retrieving the current time.
                //
                //        TODO Instead of incrementing can't we just retrieve the current
                //             time again?
                //
                // Manually update the timer.
                UptimeTimer::getInstance ().incrementElapsedTime ();

                // Copy mDeadLock and mArmed under a lock since they are shared.
                std::unique_lock<std::mutex> sl (mMutex);
                auto const deadLock = mDeadLock;
                auto const armed = mArmed;
                sl.unlock();

                // Measure the amount of time we have been deadlocked, in seconds.
                //
                // VFALCO NOTE mDeadLock is a canary for detecting the condition.
                int const timeSpentDeadlocked =
                    UptimeTimer::getInstance ().getElapsedSeconds () - deadLock;

                // VFALCO NOTE I think that "armed" refers to the deadlock detector
                //
                int const reportingIntervalSeconds = 10;
                if (armed && (timeSpentDeadlocked >= reportingIntervalSeconds))
                {
                    // Report the deadlocked condition every 10 seconds
                    if ((timeSpentDeadlocked % reportingIntervalSeconds) == 0)
                    {
                        logDeadlock (timeSpentDeadlocked);
                    }

                    // If we go over 500 seconds spent deadlocked, it means that the
                    // deadlock resolution code has failed, which qualifies as undefined
                    // behavior.
                    //
                    assert (timeSpentDeadlocked < 500);
                }
            }

            bool change;

            // VFALCO TODO Eliminate the dependence on the Application object.
            //             Choices include constructing with the job queue / feetracker.
            //             Another option is using an observer pattern to invert the dependency.
            if (getApp().getJobQueue ().isOverloaded ())
            {
                JLOG(mJournal.info) << getApp().getJobQueue ().getJson (0);
                change = getApp().getFeeTrack ().raiseLocalFee ();
            }
            else
            {
                change = getApp().getFeeTrack ().lowerLocalFee ();
            }

            if (change)
            {
                // VFALCO TODO replace this with a Listener / observer and subscribe in NetworkOPs or Application
                getApp().getOPs ().reportFeeChange ();
            }

            t += std::chrono::seconds (1);
            auto const duration = t - clock_type::now();

            if ((duration < std::chrono::seconds (0)) || (duration > std::chrono::seconds (1)))
            {
                JLOG(mJournal.warning) << "time jump";
                t = clock_type::now();
            }
            else
            {
                std::this_thread::sleep_for (duration);
            }
        }

        stopped ();
    }
};

//------------------------------------------------------------------------------

LoadManager::LoadManager (Stoppable& parent)
    : Stoppable ("LoadManager", parent)
{
}

//------------------------------------------------------------------------------

std::unique_ptr<LoadManager>
make_LoadManager (beast::Stoppable& parent, beast::Journal journal)
{
    return std::make_unique <LoadManagerImp> (parent, journal);
}

} // ripple

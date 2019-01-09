/*!
 * \file amc/ttc.cpp
 * \brief AMC TTC methods for RPC modules
 */

#include "amc/ttc.h"

#include <ios>
#include <chrono>
#include <thread>
#include <iomanip>

void ttcModuleResetLocal(localArgs* la)
{
  // writeReg(la, "GEM_AMC.TTC.CTRL.MODULE_RESET", 0x1);
}

void ttcMMCMResetLocal(localArgs* la)
{
  writeReg(la, "GEM_AMC.TTC.CTRL.MMCM_RESET", 0x1);
}

void ttcMMCMPhaseShiftLocal(localArgs* la,
                            bool shiftOutOfLockFirst,
                            bool useBC0Locked,
                            bool doScan)
{
  const int PLL_LOCK_READ_ATTEMPTS = 10;

  std::stringstream msg;
  msg << "ttcMMCMPhaseShiftLocal: Starting phase shifting procedure";
  LOGGER->log_message(LogManager::INFO, msg.str());

  std::string strTTCCtrlBaseNode = "GEM_AMC.TTC.CTRL.";
  std::vector<std::pair<std::string, uint32_t> > vec_ttcCtrlRegs;

  vec_ttcCtrlRegs.push_back(std::make_pair("DISABLE_PHASE_ALIGNMENT"       , 0x1));
  vec_ttcCtrlRegs.push_back(std::make_pair("PA_DISABLE_GTH_PHASE_TRACKING" , 0x1));
  vec_ttcCtrlRegs.push_back(std::make_pair("PA_MANUAL_OVERRIDE"            , 0x1));
  vec_ttcCtrlRegs.push_back(std::make_pair("PA_MANUAL_SHIFT_DIR"           , 0x1));
  vec_ttcCtrlRegs.push_back(std::make_pair("PA_GTH_MANUAL_OVERRIDE"        , 0x1));
  vec_ttcCtrlRegs.push_back(std::make_pair("PA_GTH_MANUAL_SHIFT_DIR"       , 0x0));
  vec_ttcCtrlRegs.push_back(std::make_pair("PA_GTH_MANUAL_SHIFT_STEP"      , 0x1));
  vec_ttcCtrlRegs.push_back(std::make_pair("PA_GTH_MANUAL_SEL_OVERRIDE"    , 0x1));
  vec_ttcCtrlRegs.push_back(std::make_pair("PA_GTH_MANUAL_COMBINED"        , 0x1));
  vec_ttcCtrlRegs.push_back(std::make_pair("GTH_TXDLYBYPASS"               , 0x1));
  vec_ttcCtrlRegs.push_back(std::make_pair("PA_MANUAL_PLL_RESET"           , 0x1));
  vec_ttcCtrlRegs.push_back(std::make_pair("CNT_RESET"                     , 0x1));

  // write & readback of aforementioned registers
  uint32_t readback;
  for (auto ttcRegIter = vec_ttcCtrlRegs.begin(); ttcRegIter != vec_ttcCtrlRegs.end(); ++ttcRegIter) {
    writeReg(la, strTTCCtrlBaseNode + (*ttcRegIter).first, (*ttcRegIter).second);
    std::this_thread::sleep_for(std::chrono::microseconds(250));
    readback = readReg(la, strTTCCtrlBaseNode + (*ttcRegIter).first);
    if (readback != (*ttcRegIter).second) {
      std::stringstream errmsg;
      errmsg << "Readback of " << strTTCCtrlBaseNode + (ttcRegIter->first).c_str()
             << " failed, value is " << readback
             << ", expected " << ttcRegIter->second;

      LOGGER->log_message(LogManager::ERROR, "ttcMMCMPhaseShiftLocal: " + errmsg.str());
      la->response->set_string("error", errmsg.str());
      return;
    }
  }

  if (readReg(la,strTTCCtrlBaseNode+"DISABLE_PHASE_ALIGNMENT") == 0x0) {
    std::stringstream errmsg;
    errmsg << "Automatic phase alignment is turned off!!";
    LOGGER->log_message(LogManager::ERROR, "ttcMMCMPhaseShift: " + errmsg.str());
    la->response->set_string("error", errmsg.str());
    return;
  }

  uint32_t readAttempts = 1;
  int maxShift = 7680+(7680/2);

  if (!useBC0Locked) {
    readAttempts = PLL_LOCK_READ_ATTEMPTS;
  }
  if (doScan) {
    readAttempts = PLL_LOCK_READ_ATTEMPTS;
    maxShift = 23040;
  }

  uint32_t mmcmShiftCnt = readReg(la,"GEM_AMC.TTC.STATUS.CLK.PA_MANUAL_SHIFT_CNT");
  uint32_t gthShiftCnt  = readReg(la,"GEM_AMC.TTC.STATUS.CLK.PA_MANUAL_GTH_SHIFT_CNT");
  int  pllLockCnt = checkPLLLockLocal(la, readAttempts);
  bool firstUnlockFound = false;
  bool nextLockFound    = false;
  bool bestLockFound    = false;
  bool reversingForLock = false;
  uint32_t phase = 0;
  double phaseNs = 0.0;

  bool mmcmShiftTable[] = {false, false, false, true, false, false, false,
                           false, false, true, false, false, false, false,
                           false, true, false, false, false, false, true,
                           false, false, false, false, false, true, false,
                           false, false, false, false, true, false, false,
                           false, false, false, true, false, false};

  int nGoodLocks       = 0;
  int nShiftsSinceLock = 0;
  int nBadLocks        = 0;
  int totalShiftCount  = 0;

  for (int i = 0; i < maxShift; ++i) {
    writeReg(la, strTTCCtrlBaseNode + "CNT_RESET", 0x1);
    writeReg(la, strTTCCtrlBaseNode + "PA_GTH_MANUAL_SHIFT_EN", 0x1);

    if (!reversingForLock && (gthShiftCnt == 39)) {
      msg.clear();
      msg.str(std::string());
      msg << "ttcMMCMPhaseShiftLocal: Normal GTH shift rollover 39->0";
      LOGGER->log_message(LogManager::DEBUG,msg.str());
      gthShiftCnt = 0;
    } else if (reversingForLock && (gthShiftCnt == 0)) {
      msg.clear();
      msg.str(std::string());
      msg << "ttcMMCMPhaseShiftLocal: Reversed GTH shift rollover 0->39";
      LOGGER->log_message(LogManager::DEBUG, msg.str());
      gthShiftCnt = 39;
    } else {
      if (reversingForLock) {
        gthShiftCnt -= 1;
      } else {
        gthShiftCnt += 1;
      }
    }

    uint32_t tmpGthShiftCnt  = readReg(la,"GEM_AMC.TTC.STATUS.CLK.PA_MANUAL_GTH_SHIFT_CNT");
    uint32_t tmpMmcmShiftCnt = readReg(la,"GEM_AMC.TTC.STATUS.CLK.PA_MANUAL_SHIFT_CNT");
    LOGGER->log_message(LogManager::INFO,stdsprintf("tmpGthShiftCnt: %i, tmpMmcmShiftCnt %i",tmpGthShiftCnt, tmpMmcmShiftCnt));
    while (gthShiftCnt != tmpGthShiftCnt) {
      msg.clear();
      msg.str(std::string());
      msg << "ttcMMCMPhaseShiftLocal: Repeating a GTH PI shift because the shift count doesn't match the expected value."
          << " Expected shift cnt = " << gthShiftCnt
          << ", ctp7 returned "       << tmpGthShiftCnt;
      LOGGER->log_message(LogManager::WARNING, msg.str());

      writeReg(la, "GEM_AMC.TTC.CTRL.PA_GTH_MANUAL_SHIFT_EN", 0x1);
      tmpGthShiftCnt = readReg(la,"GEM_AMC.TTC.STATUS.CLK.PA_MANUAL_GTH_SHIFT_CNT");
      //FIX ME should this continue indefinitely...?
    }

    if (mmcmShiftTable[gthShiftCnt+1]) {
      if (!reversingForLock && (mmcmShiftCnt == 0xffff)) {
        mmcmShiftCnt = 0;
      } else if (reversingForLock && (mmcmShiftCnt == 0x0)) {
        mmcmShiftCnt = 0xffff;
      } else {
        if (reversingForLock) {
          mmcmShiftCnt -= 1;
        } else {
          mmcmShiftCnt += 1;
        }
      }

      tmpMmcmShiftCnt = readReg(la,"TTC.STATUS.CLK.PA_MANUAL_SHIFT_CNT");
      if (mmcmShiftCnt != tmpMmcmShiftCnt) {
        msg.clear();
        msg.str(std::string());
        msg << "ttcMMCMPhaseShiftLocal: Reported MMCM shift count doesn't match the expected MMCM shift count."
            << " Expected shift cnt = " << mmcmShiftCnt
            << " , ctp7 returned "      << tmpMmcmShiftCnt;
        LOGGER->log_message(LogManager::WARNING, msg.str());
      }
    }

    pllLockCnt = checkPLLLockLocal(la,readAttempts);
    phase      = readReg(la,"GEM_AMC.TTC.STATUS.CLK.TTC_PM_PHASE_MEAN");
    phaseNs    = phase * 0.01860119;
    uint32_t gthPhase = readReg(la,"TTC.STATUS.CLK.GTH_PM_PHASE_MEAN");
    double gthPhaseNs = gthPhase * 0.01860119;

    uint32_t bc0Locked  = readReg(la,"GEM_AMC.TTC.STATUS.BC0.LOCKED");
    //uint32_t bc0UnlkCnt = readReg(la,"GEM_AMC.TTC.STATUS.BC0.UNLOCK_CNT");
    //uint32_t sglErrCnt  = readReg(la,"GEM_AMC.TTC.STATUS.TTC_SINGLE_ERROR_CNT");
    //uint32_t dblErrCnt  = readReg(la,"GEM_AMC.TTC.STATUS.TTC_DOUBLE_ERROR_CNT");

    LOGGER->log_message(LogManager::DEBUG,
                        "GTH shift #"             + std::to_string(i) +
                        ": mmcm shift cnt = "     + std::to_string(mmcmShiftCnt) +
                        ", mmcm phase counts = "  + std::to_string(phase) +
                        ", mmcm phase = "         + std::to_string(phaseNs) +
                        "ns, gth phase counts = " + std::to_string(gthPhase) +
                        ", gth phase = "          + std::to_string(gthPhaseNs) +
                        ", PLL lock count = "     + std::to_string(pllLockCnt));

    if (useBC0Locked) {
      if (!firstUnlockFound) {
        bestLockFound = false;
        if (bc0Locked == 0) {
          nBadLocks += 1;
          nGoodLocks = 0;
        } else {
          nBadLocks   = 0;
          nGoodLocks += 1;
        }

        if (shiftOutOfLockFirst) {
          if (nBadLocks > 100) {
            firstUnlockFound = true;
            msg.clear();
            msg.str(std::string());
            msg << "ttcMMCMPhaseShiftLocal: 100 unlocks found after " << i+1 << " shifts:"
                << " bad locks "              << nBadLocks
                << ", good locks "            << nGoodLocks
                << ", mmcm phase count = "    << phase
                << ", mmcm phase ns = "       << phaseNs << "ns";
            LOGGER->log_message(LogManager::INFO, msg.str());
          }
        } else {
          if (reversingForLock && (nBadLocks > 0)) {
            msg.clear();
            msg.str(std::string());
            msg << "ttcMMCMPhaseShiftLocal: Bad BC0 lock found:"
                << " phase count = " << phase
                << ", phase ns = "   << phaseNs << "ns"
                << ", returning to normal search";
            LOGGER->log_message(LogManager::DEBUG, msg.str());
            writeReg(la,"GEM_AMC.TTC.CTRL.PA_MANUAL_SHIFT_DIR",1);
            writeReg(la,"GEM_AMC.TTC.CTRL.PA_GTH_MANUAL_SHIFT_DIR",0);
            bestLockFound    = false;
            reversingForLock = false;
            nGoodLocks       = 0;
          } else if (nGoodLocks == 200) {
            reversingForLock = true;
            msg.clear();
            msg.str(std::string());
            msg << "ttcMMCMPhaseShiftLocal: 200 consecutive good BC0 locks found:"
                << " phase count = " << phase
                << ", phase ns = "   << phaseNs << "ns"
                << ", reversing scan direction";
            LOGGER->log_message(LogManager::INFO, msg.str());
            writeReg(la,"GEM_AMC.TTC.CTRL.PA_MANUAL_SHIFT_DIR",0);
            writeReg(la,"GEM_AMC.TTC.CTRL.PA_GTH_MANUAL_SHIFT_DIR",1);
          }

          if (reversingForLock && (nGoodLocks == 300)) {
            msg.clear();
            msg.str(std::string());
            msg << "ttcMMCMPhaseShiftLocal: Best lock found after reversing:"
                << " phase count = " << phase
                << ", phase ns = "   << phaseNs << "ns.";
            LOGGER->log_message(LogManager::INFO, msg.str());
            bestLockFound    = true;
            if (doScan) {
              writeReg(la,"GEM_AMC.TTC.CTRL.PA_MANUAL_SHIFT_DIR",1);
              writeReg(la,"GEM_AMC.TTC.CTRL.PA_GTH_MANUAL_SHIFT_DIR",0);
              bestLockFound    = false;
              reversingForLock = false;
              nGoodLocks       = 0;
            } else {
              break;
            }
          }
        }
      } else { // shift to first good BC0 locked
        if (bc0Locked == 0) {
          if (nextLockFound) {
            msg.clear();
            msg.str(std::string());
            msg << "ttcMMCMPhaseShiftLocal: Unexpected unlock after " << i+1 << " shifts:"
                << " bad locks "              << nBadLocks
                << ", good locks "            << nGoodLocks
                << ", mmcm phase count = "    << phase
                << ", mmcm phase ns = "       << phaseNs << "ns";
            LOGGER->log_message(LogManager::DEBUG, msg.str());
          }
          nBadLocks += 1;
        } else {
          if (!nextLockFound) {
            msg.clear();
            msg.str(std::string());
            msg << "ttcMMCMPhaseShiftLocal: Found next lock after " << i+1 << " shifts:"
                << " bad locks "            << nBadLocks
                << ", good locks "          << nGoodLocks
                << ", mmcm phase count = "  << phase
                << ", mmcm phase ns = "     << phaseNs << "ns";
            LOGGER->log_message(LogManager::INFO, msg.str());
            nextLockFound = true;
            nBadLocks   = 0;
          }
          nGoodLocks += 1;
        }

        if (nGoodLocks == 1920) {
          msg.clear();
          msg.str(std::string());
          msg << "ttcMMCMPhaseShiftLocal: Finished 1920 shifts after first good lock: "
              << "bad locks "   << nBadLocks
              << " good locks " << nGoodLocks;
          LOGGER->log_message(LogManager::INFO, msg.str());
          bestLockFound = true;
          if (doScan) {
            nextLockFound    = false;
            firstUnlockFound = false;
            nGoodLocks       = 0;
            nBadLocks        = 0;
            nShiftsSinceLock = 0;
          } else {
            break;
          }
        }
      }
    } else if (true) { // using the PLL lock counter, but the method as for the BC0 lock
      if (!firstUnlockFound) {
        bestLockFound = false;
        if (pllLockCnt < PLL_LOCK_READ_ATTEMPTS) {
          nBadLocks += 1;
          nGoodLocks = 0;
        } else {
          nBadLocks   = 0;
          nGoodLocks += 1;
        }

        if (shiftOutOfLockFirst) {
          if (nBadLocks > 500) {
            firstUnlockFound = true;
            msg.clear();
            msg.str(std::string());
            msg << "ttcMMCMPhaseShiftLocal: 500 unlocks found after " << i+1 << " shifts:"
                << " bad locks "              << nBadLocks
                << ", good locks "            << nGoodLocks
                << ", mmcm phase count = "    << phase
                << ", mmcm phase ns = "       << phaseNs << "ns";
            LOGGER->log_message(LogManager::DEBUG, msg.str());
          } else {
            if (reversingForLock && (nBadLocks > 0)) {
              msg.clear();
              msg.str(std::string());
              msg << "ttcMMCMPhaseShiftLocal: Bad BC0 lock found:"
                  << " phase count = " << phase
                  << ", phase ns = "   << phaseNs << "ns"
                  << ", returning to normal search";
              LOGGER->log_message(LogManager::DEBUG, msg.str());
              writeReg(la,"GEM_AMC.TTC.CTRL.PA_MANUAL_SHIFT_DIR",1);
              writeReg(la,"GEM_AMC.TTC.CTRL.PA_GTH_MANUAL_SHIFT_DIR",0);
              bestLockFound    = false;
              reversingForLock = false;
              nGoodLocks       = 0;
            } else if (nGoodLocks == 50) {
              reversingForLock = true;
              msg.clear();
              msg.str(std::string());
              msg << "ttcMMCMPhaseShiftLocal: 50 consecutive good PLL locks found:"
                  << " phase count = " << phase
                  << ", phase ns = "   << phaseNs << "ns"
                  << ", reversing scan direction";
              LOGGER->log_message(LogManager::INFO, msg.str());
              writeReg(la,"GEM_AMC.TTC.CTRL.PA_MANUAL_SHIFT_DIR",0);
              writeReg(la,"GEM_AMC.TTC.CTRL.PA_GTH_MANUAL_SHIFT_DIR",1);
            }

            if (reversingForLock &&(nGoodLocks == 75)) {
              msg.clear();
              msg.str(std::string());
              msg << "ttcMMCMPhaseShiftLocal: Best lock found after reversing:"
                  << " phase count = " << phase
                  << ", phase ns = "   << phaseNs << "ns.";
              LOGGER->log_message(LogManager::INFO, msg.str());
              bestLockFound = true;
              if (doScan) {
                writeReg(la,"GEM_AMC.TTC.CTRL.PA_MANUAL_SHIFT_DIR",1);
                writeReg(la,"GEM_AMC.TTC.CTRL.PA_GTH_MANUAL_SHIFT_DIR",0);
                bestLockFound    = false;
                reversingForLock = false;
                nGoodLocks       = 0;
              } else {
                break;
              }
            }
          }
        }
      } else { // shift to first good PLL locked
        if (pllLockCnt < PLL_LOCK_READ_ATTEMPTS) {
          if (nextLockFound) {
            msg.clear();
            msg.str(std::string());
            msg << "ttcMMCMPhaseShiftLocal: Unexpected unlock after " << i+1 << " shifts:"
                << " bad locks "              << nBadLocks
                << ", good locks "            << nGoodLocks
                << ", mmcm phase count = "    << phase
                << ", mmcm phase ns = "       << phaseNs << "ns";
            LOGGER->log_message(LogManager::WARNING, msg.str());
            nBadLocks += 1;
          } else {
            if (!nextLockFound) {
              msg.clear();
              msg.str(std::string());
              msg << "ttcMMCMPhaseShiftLocal: Found next lock after " << i+1 << " shifts:"
                  << " bad locks "            << nBadLocks
                  << ", good locks "          << nGoodLocks
                  << ", mmcm phase count = "  << phase
                  << ", mmcm phase ns = "     << phaseNs << "ns";
              LOGGER->log_message(LogManager::INFO, msg.str());
              nextLockFound = true;
              nBadLocks     = 0;
            }
            nGoodLocks += 1;
          }

          if (nShiftsSinceLock == 1000) {
            msg.clear();
            msg.str(std::string());
            msg << "ttcMMCMPhaseShiftLocal: Finished 1000 shifts after first good lock:"
                << " bad locks "   << nBadLocks
                << ", good locks " << nGoodLocks;
            LOGGER->log_message(LogManager::INFO, msg.str());
            bestLockFound = true;
            if (doScan) {
              nextLockFound    = false;
              firstUnlockFound = false;
              nGoodLocks       = 0;
              nBadLocks        = 0;
              nShiftsSinceLock = 0;
            } else {
              break;
            }
          }
        }
      }
    } else {
      if (shiftOutOfLockFirst && (pllLockCnt < PLL_LOCK_READ_ATTEMPTS) && !firstUnlockFound) {
        firstUnlockFound = true;
        msg.clear();
        msg.str(std::string());
        msg << "ttcMMCMPhaseShiftLocal: Unlocked after "        << i+1 << "shifts:"
            << " mmcm phase count = "     << phase
            << ", mmcm phase ns = "       << phaseNs << "ns"
            << ", pllLockCnt = "          << pllLockCnt
            << ", firstUnlockFound = "    << firstUnlockFound
            << ", shiftOutOfLockFirst = " << shiftOutOfLockFirst;
        LOGGER->log_message(LogManager::WARNING, msg.str());
      }

      if (pllLockCnt == PLL_LOCK_READ_ATTEMPTS) {
        if (!shiftOutOfLockFirst) {
          if (nGoodLocks == 50) {
            reversingForLock = true;
            msg.clear();
            msg.str(std::string());
            msg << "ttcMMCMPhaseShiftLocal: 200 consecutive good PLL locks found:"
                << " phase count = " << phase
                << ", phase ns = "   << phaseNs << "ns"
                << ", reversing scan direction";
            LOGGER->log_message(LogManager::INFO, msg.str());
            writeReg(la,"GEM_AMC.TTC.CTRL.PA_MANUAL_SHIFT_DIR",0);
            writeReg(la,"GEM_AMC.TTC.CTRL.PA_GTH_MANUAL_SHIFT_DIR",1);
          }

          if (reversingForLock && (nGoodLocks == 75)) {
            msg.clear();
            msg.str(std::string());
            msg << "ttcMMCMPhaseShiftLocal: Best lock found after reversing:"
                << " phase count = " << phase
                << ", phase ns = "   << phaseNs << "ns.";

            LOGGER->log_message(LogManager::INFO, msg.str());
            bestLockFound    = true;
            if (doScan) {
              writeReg(la,"GEM_AMC.TTC.CTRL.PA_MANUAL_SHIFT_DIR",1);
              writeReg(la,"GEM_AMC.TTC.CTRL.PA_GTH_MANUAL_SHIFT_DIR",0);
              bestLockFound    = false;
              reversingForLock = false;
              nGoodLocks       = 0;
              nShiftsSinceLock = 0;
            } else {
              break;
            }
          }
        } else if (firstUnlockFound || !shiftOutOfLockFirst) {
          if (!nextLockFound) {
            msg.clear();
            msg.str(std::string());
            msg << "ttcMMCMPhaseShiftLocal: Found next lock after " << i+1 << " shifts:"
                << " bad locks "            << nBadLocks
                << ", good locks "          << nGoodLocks
                << ", mmcm phase count = "  << phase
                << ", mmcm phase ns = "     << phaseNs << "ns";
            LOGGER->log_message(LogManager::DEBUG, msg.str());
            nextLockFound = true;
          }

          if (nShiftsSinceLock > 500) {
            bestLockFound = true;
            if (!doScan)
              break;
            nextLockFound    = false;
            firstUnlockFound = false;
            bestLockFound    = false;
            nGoodLocks       = 0;
            nShiftsSinceLock = 0;
          }
        } else {
          nGoodLocks += 1;
        }
      } else if (nextLockFound) {
        if (nShiftsSinceLock > 500) {
          bestLockFound = true;
          if (!doScan)
            break;
          nextLockFound    = false;
          firstUnlockFound = false;
          bestLockFound    = false;
          nGoodLocks       = 0;
          nShiftsSinceLock = 0;
        }
      } else {
        bestLockFound = false;
        nBadLocks += 1;
      }
    }

    if (nextLockFound) {
      nShiftsSinceLock += 1;
    }

    if (reversingForLock) {
      totalShiftCount -= 1;
    } else{
      totalShiftCount += 1;
    }
  }

  if (bestLockFound) {
    writeReg(la,"GEM_AMC.TTC.CTRL.MMCM_RESET", 0x1);
    std::stringstream msg;
    msg << "ttcMMCMPhaseShiftLocal: Lock was found: phase count " << phase
        << ", phase " << phaseNs << "ns";

    LOGGER->log_message(LogManager::INFO, msg.str());
  } else {
    std::stringstream errmsg;
    errmsg << "Unable to find lock";
    LOGGER->log_message(LogManager::ERROR, "ttcMMCMPhaseShift: " + errmsg.str());
    la->response->set_string("error",errmsg.str());
  }

  return;
}

int checkPLLLockLocal(localArgs* la, int readAttempts)
{
  uint32_t lockCnt = 0;
  std::stringstream msg;
  msg << "Executing checkPLLLock with " << readAttempts << " attempted relocks";
  LOGGER->log_message(LogManager::DEBUG, msg.str());
  for (uint32_t i = 0; i < readAttempts; ++i ) {
    writeReg(la,"GEM_AMC.TTC.CTRL.PA_MANUAL_PLL_RESET", 0x1);

    // wait 100us to allow the PLL to lock
    std::this_thread::sleep_for(std::chrono::microseconds(100));

    // Check if it's locked
    if (readReg(la,"GEM_AMC.TTC.STATUS.CLK.PHASE_LOCKED") != 0) {
      lockCnt += 1;
    }
  }
  return lockCnt;
}

uint32_t getMMCMPhaseMeanLocal(localArgs* la)
{
  LOGGER->log_message(LogManager::WARNING,"getMMCMPhaseMean not yet implemented");
  return 0x0;
}

uint32_t getGTHPhaseMeanLocal(localArgs* la)
{
  LOGGER->log_message(LogManager::WARNING,"getGTHPhaseMean not yet implemented");
  return 0x0;
}

uint32_t getMMCMPhaseMedianLocal(localArgs* la)
{
  LOGGER->log_message(LogManager::WARNING,"getMMCMPhaseMedian not yet implemented");
  return 0x0;
}

uint32_t getGTHPhaseMedianLocal(localArgs* la)
{
  LOGGER->log_message(LogManager::WARNING,"getGTHPhaseMedian not yet implemented");
  return 0x0;
}

void ttcCounterResetLocal(localArgs* la)
{
  writeReg(la, "GEM_AMC.TTC.CTRL.CNT_RESET", 0x1);
}

bool getL1AEnableLocal(localArgs* la)
{
  return readReg(la, "GEM_AMC.TTC.CTRL.L1A_ENABLE");
}

void setL1AEnableLocal(localArgs* la,
                       bool enable)
{
  writeReg(la, "GEM_AMC.TTC.CTRL.L1A_ENABLE", int(enable));
}

/*** CONFIG submodule ***/
uint32_t getTTCConfigLocal(localArgs* la, uint8_t const& cmd)
{
  LOGGER->log_message(LogManager::WARNING,"getTTCConfig not implemented");
  // return readReg(la, "GEM_AMC.TTC.CTRL.L1A_ENABLE");
  return 0x0;
}

void setTTCConfigLocal(localArgs* la,
                       uint8_t const& cmd,
                       uint8_t const& value)
{
  LOGGER->log_message(LogManager::WARNING,"setTTCConfig not implemented");
  // return writeReg(la, "GEM_AMC.TTC.CTRL.L1A_ENABLE",value);
}

/*** STATUS submodule ***/
uint32_t getTTCStatusLocal(localArgs* la)
{
  LOGGER->log_message(LogManager::WARNING,"getTTCStatusLocal not fully implemented");
  // uint32_t retval = readReg(la, "GEM_AMC.TTC.STATUS");
  uint32_t retval = readReg(la, "GEM_AMC.TTC.STATUS.BC0.LOCKED");
  std::stringstream msg;
  msg << "getTTCStatusLocal TTC status reads " << std::hex << std::setw(8) << retval << std::dec;
  LOGGER->log_message(LogManager::DEBUG,msg.str());
  return retval;
}

uint32_t getTTCErrorCountLocal(localArgs* la,
                               bool const& single)
{
  if (single)
    return readReg(la, "GEM_AMC.TTC.STATUS.TTC_SINGLE_ERROR_CNT");
  else
    return readReg(la, "GEM_AMC.TTC.STATUS.TTC_DOUBLE_ERROR_CNT");
}

/*** CMD_COUNTERS submodule ***/
uint32_t getTTCCounterLocal(localArgs* la,
                            uint8_t const& cmd)
{
  switch(cmd) {
  case(0x1) :
    return readReg(la,"GEM_AMC.TTC.CMD_COUNTERS.L1A");
  case(0x2) :
    return readReg(la,"GEM_AMC.TTC.CMD_COUNTERS.BC0");
  case(0x3) :
    return readReg(la,"GEM_AMC.TTC.CMD_COUNTERS.EC0");
  case(0x4) :
    return readReg(la,"GEM_AMC.TTC.CMD_COUNTERS.RESYNC");
  case(0x5) :
    return readReg(la,"GEM_AMC.TTC.CMD_COUNTERS.OC0");
  case(0x6) :
    return readReg(la,"GEM_AMC.TTC.CMD_COUNTERS.HARD_RESET");
  case(0x7) :
    return readReg(la,"GEM_AMC.TTC.CMD_COUNTERS.CALPULSE");
  case(0x8) :
    return readReg(la,"GEM_AMC.TTC.CMD_COUNTERS.START");
  case(0x9) :
    return readReg(la,"GEM_AMC.TTC.CMD_COUNTERS.STOP");
  case(0xa) :
    return readReg(la,"GEM_AMC.TTC.CMD_COUNTERS.TEST_SYNC");
  default :
    std::array<std::string,10> counters = {{"L1A","BC0","EC0","RESYNC","OC0","HARD_RESET","CALPULSE","START","STOP","TEST_SYNC"}};
    for (auto& cnt: counters)
      la->response->set_word(cnt,readReg(la,"GEM_AMC.TTC.CMD_COUNTERS."+cnt));
    return readReg(la,"GEM_AMC.TTC.CMD_COUNTERS.L1A");
  }
}

uint32_t getL1AIDLocal(localArgs* la)
{
  return readReg(la,"GEM_AMC.TTC.L1A_ID");
}

uint32_t getL1ARateLocal(localArgs* la)
{
  return readReg(la,"GEM_AMC.TTC.L1A_RATE");
}

uint32_t getTTCSpyBufferLocal(localArgs* la)
{
  LOGGER->log_message(LogManager::WARNING,"getTTCSpyBuffer is obsolete");
  // return readReg(la,"GEM_AMC.TTC.TTC_SPY_BUFFER");
  return 0x0;
}

/** RPC callbacks */
void ttcModuleReset(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
  ttcModuleResetLocal(&la);
}

void ttcMMCMReset(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
  // LocalArgs la = getLocalArgs(response);
  ttcMMCMResetLocal(&la);
}

void ttcMMCMPhaseShift(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);

  bool shiftOutOfLockFirst = request->get_word("shiftOutOfLockFirst");
  bool useBC0Locked        = request->get_word("useBC0Locked");
  bool doScan              = request->get_word("doScan");

  ttcMMCMPhaseShiftLocal(&la, shiftOutOfLockFirst, useBC0Locked, doScan);

  return;
}

void checkPLLLock(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);

  uint32_t readAttempts = request->get_word("readAttempts");
  uint32_t lockCnt      = checkPLLLockLocal(&la, readAttempts);
  std::stringstream msg;
  msg << "Checked PLL Locked Status " << readAttempts << " times."
      << " Found PLL Locked " << lockCnt << " times";
  LOGGER->log_message(LogManager::INFO, msg.str());

  response->set_word("lockCnt",lockCnt);

  return;
}

void getMMCMPhaseMean(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
}

void getMMCMPhaseMedian(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
}

void getGTHPhaseMean(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
}

void getGTHPhaseMedian(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
}

void ttcCounterReset(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
  ttcCounterResetLocal(&la);
}
void getL1AEnable(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
  getL1AEnableLocal(&la);
}

void setL1AEnable(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
  bool en = request->get_word("enable");
  setL1AEnableLocal(&la, en);
}

void getTTCConfig(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
  uint8_t cmd = request->get_word("cmd");
  getTTCConfigLocal(&la, cmd);
}

void setTTCConfig(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
  uint8_t cmd = request->get_word("cmd");
  uint8_t val = request->get_word("value");
  setTTCConfigLocal(&la, cmd, val);
}

void getTTCStatus(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
  uint32_t status = getTTCStatusLocal(&la);
  // uint32_t status = 0xdeadbeef;
  response->set_word("result",status);
}

void getTTCErrorCount(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
  bool single = request->get_word("single");
  getTTCErrorCountLocal(&la, single);
}

void getTTCCounter(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
  uint8_t cmd = request->get_word("cmd");
  getTTCCounterLocal(&la, cmd);
}

void getL1AID(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
  getL1AIDLocal(&la);
}

void getL1ARate(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
  getL1ARateLocal(&la);
}

void getTTCSpyBuffer(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);
  getTTCSpyBufferLocal(&la);
}

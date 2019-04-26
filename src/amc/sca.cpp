/*!
 * \file amc/sca.cpp
 * \brief AMC SCA methods for RPC modules
 */

#include "amc/sca.h"

uint32_t formatSCAData(uint32_t const& data)
{
  return (
          ((data&0xff000000)>>24) +
          ((data>>8)&0x0000ff00)  +
          ((data&0x0000ff00)<<8) +
          ((data&0x000000ff)<<24)
          );
}

void sendSCACommand(localArgs* la, uint8_t const& ch, uint8_t const& cmd, uint8_t const& len, uint32_t data, uint16_t const& ohMask)
{
  // FIXME: DECIDE WHETHER TO HAVE HERE // writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF",         0xffffffff);
  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.MANUAL_CONTROL.LINK_ENABLE_MASK",       ohMask);
  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.MANUAL_CONTROL.SCA_CMD.SCA_CMD_CHANNEL",ch);
  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.MANUAL_CONTROL.SCA_CMD.SCA_CMD_COMMAND",cmd);
  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.MANUAL_CONTROL.SCA_CMD.SCA_CMD_LENGTH", len);
  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.MANUAL_CONTROL.SCA_CMD.SCA_CMD_DATA",   formatSCAData(data));
  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.MANUAL_CONTROL.SCA_CMD.SCA_CMD_EXECUTE",0x1);
}

std::vector<uint32_t> sendSCACommandWithReply(localArgs* la, uint8_t const& ch, uint8_t const& cmd, uint8_t const& len, uint32_t data, uint16_t const& ohMask)
{
  // FIXME: DECIDE WHETHER TO HAVE HERE // uint32_t monMask = readReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF");
  // FIXME: DECIDE WHETHER TO HAVE HERE // writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF",       0xffffffff);

  sendSCACommand(la, ch, cmd, len, data, ohMask);

  // read reply from 12 OptoHybrids
  std::vector<uint32_t> reply;
  reply.reserve(12);
  for (size_t oh = 0; oh < 12; ++oh) {
    if ((ohMask >> oh) & 0x1) {
      std::stringstream regName;
      regName << "GEM_AMC.SLOW_CONTROL.SCA.MANUAL_CONTROL.SCA_REPLY_OH"
              << oh << ".SCA_RPY_DATA";
      reply.push_back(formatSCAData(readReg(la,regName.str())));
    } else {
      // FIXME Sensible null value?
      reply.push_back(0);
    }
  }
  // FIXME: DECIDE WHETHER TO HAVE HERE // writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF", monMask);
  return reply;
}

std::vector<uint32_t> scaCTRLCommand(localArgs* la, SCACTRLCommandT const& cmd, uint16_t const& ohMask, uint8_t const& len, uint32_t const& data)
{
  uint32_t monMask = readReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF");
  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF",       0xffffffff);

  std::vector<uint32_t> result;
  switch (cmd) {
  case SCACTRLCommand::CTRL_R_ID_V2:
    result = sendSCACommandWithReply(la, 0x14, cmd, 0x1, 0x1, ohMask);
  case SCACTRLCommand::CTRL_R_ID_V1:
    result = sendSCACommandWithReply(la, 0x14, cmd, 0x1, 0x1, ohMask);
  case SCACTRLCommand::CTRL_R_SEU:
    result = sendSCACommandWithReply(la, 0x13, cmd, 0x1, 0x0, ohMask);
  case SCACTRLCommand::CTRL_C_SEU:
    result = sendSCACommandWithReply(la, 0x13, cmd, 0x1, 0x0, ohMask);
  case SCACTRLCommand::CTRL_W_CRB:
    sendSCACommand(la, SCAChannel::CTRL, cmd, len, data, ohMask);
  case SCACTRLCommand::CTRL_W_CRC:
    sendSCACommand(la, SCAChannel::CTRL, cmd, len, data, ohMask);
  case SCACTRLCommand::CTRL_W_CRD:
    sendSCACommand(la, SCAChannel::CTRL, cmd, len, data, ohMask);
  case SCACTRLCommand::CTRL_R_CRB:
    result = sendSCACommandWithReply(la, SCAChannel::CTRL, cmd, len, data, ohMask);
  case SCACTRLCommand::CTRL_R_CRC:
    result = sendSCACommandWithReply(la, SCAChannel::CTRL, cmd, len, data, ohMask);
  case SCACTRLCommand::CTRL_R_CRD:
    result = sendSCACommandWithReply(la, SCAChannel::CTRL, cmd, len, data, ohMask);
  default:
    // maybe don't do this by default, return error or invalid option?
    result = sendSCACommandWithReply(la, SCAChannel::CTRL, SCACTRLCommand::GET_DATA, len, data, ohMask);
  }

  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF", monMask);
  return result;
}

std::vector<uint32_t> scaI2CCommand(localArgs* la, SCAI2CChannelT const& ch, SCAI2CCommandT const& cmd, uint8_t const& len, uint32_t data, uint16_t const& ohMask)
{

  // enable the I2C bus through the CTRL CR{B,C,D} registers
  // 16 I2C channels available
  // 4 I2C modes of communication
  // I2C frequency selection
  // Allows RMW transactions

  std::vector<uint32_t> result;

  uint32_t monMask = readReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF");
  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF",       0xffffffff);

  sendSCACommand(la, ch, cmd, len, data, ohMask);

  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF", monMask);

  return result;
}

std::vector<uint32_t> scaGPIOCommandLocal(localArgs* la, SCAGPIOCommandT const& cmd, uint8_t const& len, uint32_t data, uint16_t const& ohMask)
{
  // enable the GPIO bus through the CTRL CRB register, bit 2
  uint32_t monMask = readReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF");
  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF",       0xffffffff);

  std::vector<uint32_t> reply = sendSCACommandWithReply(la, SCAChannel::GPIO, cmd, len, data, ohMask);

  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF", monMask);
  return reply;
}

std::vector<uint32_t> scaADCCommand(localArgs* la, SCAADCChannelT const& ch, uint8_t const& len, uint32_t data, uint16_t const& ohMask)
{
  uint32_t monMask = readReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF");
  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF",       0xffffffff);

  // enable the ADC bus through the CTRL CRD register, bit 4
  // select the ADC channel
  sendSCACommand(la, SCAChannel::ADC, SCAADCCommand::ADC_W_MUX, 0x4, ch, ohMask);


  // enable/disable the current sink if reading the temperature ADCs
  if (ch == 0x00 || ch == 0x04 || ch == 0x07 || ch == 0x08 || ch == 0x1f)	// Hardcoded the channel numbers
    sendSCACommand(la, SCAChannel::ADC, SCAADCCommand::ADC_W_CURR, 0x4, 0x1<<ch, ohMask);
  // start the conversion and get the result
  std::vector<uint32_t> result = sendSCACommandWithReply(la, SCAChannel::ADC, SCAADCCommand::ADC_GO, 0x4, 0x1, ohMask);

  // // get the last data
  // std::vector<uint32_t> last = sendSCACommandWithReply(la, SCAChannel::ADC, SCAADCCommand::ADC_R_DATA, 0x1, 0x0, ohMask);
  // // get the raw data
  // std::vector<uint32_t> raw  = sendSCACommandWithReply(la, SCAChannel::ADC, SCAADCCommand::ADC_R_RAW, 0x1, 0x0, ohMask);
  // // get the offset
  // std::vector<uint32_t> raw  = sendSCACommandWithReply(la, SCAChannel::ADC, SCAADCCommand::ADC_R_OFS, 0x1, 0x0, ohMask);

  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.ADC_MONITORING.MONITORING_OFF", monMask);

  return result;
}

std::vector<uint32_t> readSCAChipIDLocal(localArgs* la, uint16_t const& ohMask, bool scaV1)
{
  if (scaV1)
    return scaCTRLCommand(la, SCACTRLCommand::CTRL_R_ID_V1, ohMask);
  else
    return scaCTRLCommand(la, SCACTRLCommand::CTRL_R_ID_V2, ohMask);

  // ID = DATA[23:0], need to have this set up in the reply function somehow?
}

std::vector<uint32_t> readSCASEUCounterLocal(localArgs* la, uint16_t const& ohMask, bool reset)
{
  if (reset)
    resetSCASEUCounterLocal(la, ohMask);

  return scaCTRLCommand(la, SCACTRLCommand::CTRL_R_SEU, ohMask);

  // SEU count = DATA[31:0]
}

void resetSCASEUCounterLocal(localArgs* la, uint16_t const& ohMask)
{
  scaCTRLCommand(la, SCACTRLCommand::CTRL_C_SEU, ohMask);
}

void scaModuleResetLocal(localArgs* la, uint16_t const& ohMask)
{
  // Does this need to be protected, or do all versions of FW have this register?
  uint32_t origMask = readReg(la,"GEM_AMC.SLOW_CONTROL.SCA.CTRL.SCA_RESET_ENABLE_MASK");
  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.CTRL.SCA_RESET_ENABLE_MASK", ohMask);

  // Send the reset
  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.CTRL.MODULE_RESET", 0x1);

  writeReg(la,"GEM_AMC.SLOW_CONTROL.SCA.CTRL.SCA_RESET_ENABLE_MASK", origMask);
}

void scaHardResetEnableLocal(localArgs* la, bool en)
{
  writeReg(la, "GEM_AMC.SLOW_CONTROL.SCA.CTRL.TTC_HARD_RESET_EN", uint32_t(en));
}

/** RPC callbacks */
void scaModuleReset(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);

  uint32_t ohMask = request->get_word("ohMask");

  scaModuleResetLocal(&la, ohMask);

  rtxn.abort();
}

void readSCAChipID(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);

  uint32_t ohMask = request->get_word("ohMask");
  bool     scaV1  = request->get_word("scaV1");
  std::vector<uint32_t> scaChipIDs = readSCAChipIDLocal(&la, ohMask, scaV1);

  rtxn.abort();
}

void readSCASEUCounter(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);

  uint32_t ohMask = request->get_word("ohMask");
  bool     reset  = request->get_word("reset");
  std::vector<uint32_t> seuCounts = readSCASEUCounterLocal(&la, ohMask, reset);

  rtxn.abort();
}

void resetSCASEUCounter(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);

  uint32_t ohMask = request->get_word("ohMask");
  resetSCASEUCounterLocal(&la, ohMask);

  rtxn.abort();
}

void readADCCommands(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);

  uint32_t ohMask = request->get_word("ohMask");
  LOGGER->log_message(LogManager::INFO, stdsprintf("Optohybrids to read: %x",ohMask));

  SCAADCChannelT  ch = static_cast<SCAADCChannelT>(request->get_word("ch"));
  LOGGER->log_message(LogManager::INFO, stdsprintf("ADC Channel to read: %x",ch));
  /////
  std::vector<uint32_t> result;
  result = scaADCCommand(&la, ch, 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Value for OH%i = %i ",i,result[i])); }

  rtxn.abort();
}

void readADCTemperatureChannel(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);

  uint32_t ohMask = request->get_word("ohMask");
  LOGGER->log_message(LogManager::INFO, stdsprintf("Optohybrids to read: %x",ohMask));

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature ADC Channel to read: 0x00"));
  std::vector<uint32_t> result;
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x00), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature for OH%i, SCA-ADC channel 0x00 = %i ",i,result[i])); 
    }

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature ADC Channel to read: 0x04"));
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x04), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature for OH%i, SCA-ADC channel 0x04 = %i ",i,result[i])); 
    }

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature ADC Channel to read: 0x07"));
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x07), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature for OH%i, SCA-ADC channel 0x07 = %i ",i,result[i])); 
    }

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature ADC Channel to read: 0x08"));
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x08), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature for OH%i, SCA-ADC channel 0x08 = %i ",i,result[i])); 
    }

  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x1f), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Internal SCA temperature for OH%i, SCA-ADC channel 0x1f = %i ",i,result[i])); 
    }

  rtxn.abort();
}

void readADCVoltageChannel(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);

  uint32_t ohMask = request->get_word("ohMask");
  LOGGER->log_message(LogManager::INFO, stdsprintf("Optohybrids to read: %x",ohMask));

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Voltage ADC Channel to read: 0x00"));
  std::vector<uint32_t> result;
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x1b), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("FPGA MGT Voltage for OH%i, SCA-ADC channel 0x1b = %i ",i,result[i])); 
    }

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Voltage ADC Channel to read: 0x04"));
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x1e), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("FPGA MGT Voltage for OH%i, SCA-ADC channel 0x1e = %i ",i,result[i])); 
    }

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Voltage ADC Channel to read: 0x07"));
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x11), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("FPGA core voltage for OH%i, SCA-ADC channel 0x11 = %i ",i,result[i])); 
    }

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Voltage ADC Channel to read: 0x08"));
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x0e), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("PROM power voltage for OH%i, SCA-ADC channel 0x0e = %i ",i,result[i])); 
    }

  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x18), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("power for GBTX and SCA for OH%i, SCA-ADC channel 0x18 = %i ",i,result[i])); 
    }

  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x0f), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("FPGA I/O power for OH%i, SCA-ADC channel 0x0f = %i ",i,result[i])); 
    }

  rtxn.abort();
}

void readADCSignalStrengthChannel(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);

  uint32_t ohMask = request->get_word("ohMask");
  LOGGER->log_message(LogManager::INFO, stdsprintf("Optohybrids to read: %x",ohMask));

  std::vector<uint32_t> result;
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x15), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Signal strength of VTRX1 OH%i, SCA-ADC channel 0x15 = %i ",i,result[i])); 
    }

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature ADC Channel to read: 0x04"));
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x13), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Signal strength of VTRX2 OH%i, SCA-ADC channel 0x13 = %i ",i,result[i])); 
    }

  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x12), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Signal strength of VTRX3 OH%i, SCA-ADC channel 0x12 = %i ",i,result[i])); 
    }

  rtxn.abort();
}

void readAllADCChannel(const RPCMsg *request, RPCMsg *response)
{
  // struct localArgs la = getLocalArgs(response);
  GETLOCALARGS(response);

  uint32_t ohMask = request->get_word("ohMask");
  LOGGER->log_message(LogManager::INFO, stdsprintf("Optohybrids to read: %x",ohMask));

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature ADC Channel to read: 0x00"));
  std::vector<uint32_t> result;
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x00), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature for OH%i, SCA-ADC channel 0x00 = %i ",i,result[i])); 
    }

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature ADC Channel to read: 0x04"));
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x04), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature for OH%i, SCA-ADC channel 0x04 = %i ",i,result[i])); 
    }

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature ADC Channel to read: 0x07"));
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x07), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature for OH%i, SCA-ADC channel 0x07 = %i ",i,result[i])); 
    }

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature ADC Channel to read: 0x08"));
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x08), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature for OH%i, SCA-ADC channel 0x08 = %i ",i,result[i])); 
    }

  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x1f), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Internal SCA temperature for OH%i, SCA-ADC channel 0x1f = %i ",i,result[i])); 
    }

  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x1b), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("FPGA MGT Voltage for OH%i, SCA-ADC channel 0x1b = %i ",i,result[i])); 
    }

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Voltage ADC Channel to read: 0x04"));
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x1e), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("FPGA MGT Voltage for OH%i, SCA-ADC channel 0x1e = %i ",i,result[i])); 
    }

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Voltage ADC Channel to read: 0x07"));
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x11), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("FPGA core voltage for OH%i, SCA-ADC channel 0x11 = %i ",i,result[i])); 
    }

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Voltage ADC Channel to read: 0x08"));
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x0e), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("PROM power voltage for OH%i, SCA-ADC channel 0x0e = %i ",i,result[i])); 
    }

  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x18), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("power for GBTX and SCA for OH%i, SCA-ADC channel 0x18 = %i ",i,result[i])); 
    }

  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x0f), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("FPGA I/O power for OH%i, SCA-ADC channel 0x0f = %i ",i,result[i])); 
    }

  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x15), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Signal strength of VTRX1 OH%i, SCA-ADC channel 0x15 = %i ",i,result[i])); 
    }

  //LOGGER->log_message(LogManager::INFO, stdsprintf("Temperature ADC Channel to read: 0x04"));
  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x13), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Signal strength of VTRX2 OH%i, SCA-ADC channel 0x13 = %i ",i,result[i])); 
    }

  result = scaADCCommand(&la, static_cast<SCAADCChannelT>(0x12), 0, 0, ohMask);
  for(unsigned int i=0; i<result.size(); i++)
    { 
      if (result[i] != 0)
      LOGGER->log_message(LogManager::INFO, stdsprintf("Signal strength of VTRX3 OH%i, SCA-ADC channel 0x12 = %i ",i,result[i])); 
    }

  rtxn.abort();
}


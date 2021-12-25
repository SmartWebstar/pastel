// Copyright (c) 2018-2021 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <inttypes.h>
#include <json/json.hpp>

#include <init.h>
#include <pastelid/common.h>
#include <mnode/tickets/etherium-address-change.h>
#include <mnode/ticket-processor.h>
#include <mnode/mnode-controller.h>
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif // ENABLE_WALLET

using json = nlohmann::json;
using namespace std;

// CChangeEthereumAddressTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
string CChangeEthereumAddressTicket::ToJSON() const noexcept
{
    const json jsonObj = 
    {
        {"txid", m_txid},
        {"height", m_nBlock},
        {"ticket", 
            {
                {"type", GetTicketName()}, 
                {"pastelID", pastelID}, 
                {"ethereumAddress", ethereumAddress}, 
                {"fee", fee}, 
                {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
            }
        }
    };

    return jsonObj.dump(4);
}

string CChangeEthereumAddressTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << pastelID;
    ss << ethereumAddress;
    ss << fee;
    ss << m_nTimestamp;
    return ss.str();
}

bool CChangeEthereumAddressTicket::IsValid(const bool bPreReg, const int nDepth) const
{
    const unsigned int chainHeight = GetActiveChainHeight();

    CChangeEthereumAddressTicket existingTicket;
    bool ticketExists = FindTicketInDb(ethereumAddress, existingTicket);
    // A. Something to check ONLY before ticket made into transaction
    if (bPreReg) {
        // A2. Check if address has coins to pay for Ethereum Address Change Ticket
        const auto fullTicketPrice = TicketPrice(chainHeight);

        if (pwalletMain->GetBalance() < fullTicketPrice * COIN) {
            throw runtime_error(strprintf("Not enough coins to cover price [%" PRId64 "]", fullTicketPrice));
        }
    }

    // Check if Ethereum Address is an invalid address. For now check if it is empty only.
    string invalidEthereumAddressError;
    if (isEthereumAddressInvalid(ethereumAddress, invalidEthereumAddressError)) {
        throw runtime_error(invalidEthereumAddressError.c_str());
    }

    // B Verify signature
    // We will check that it is the correct PastelID
    string strThisTicket = ToStr();
    if (!CPastelID::Verify(strThisTicket, vector_to_string(signature), pastelID)) {
        throw runtime_error(strprintf("%s ticket's signature is invalid. PastelID - [%s]", GetTicketDescription(TicketID::EthereumAddress), pastelID));
    }
    // C (ticket transaction replay attack protection)
    if ((ticketExists) &&
        (!existingTicket.IsBlock(m_nBlock) || existingTicket.m_txid != m_txid) &&
        masterNodeCtrl.masternodeTickets.getValueBySecondaryKey(existingTicket) == ethereumAddress) {
        throw runtime_error(strprintf("This Ethereum Address Change Request is already registered in blockchain [Ethereum Address = %s]"
                                           "[this ticket block = %u txid = %s; found ticket block  = %u txid = %s]",
                                           ethereumAddress, existingTicket.GetBlock(), existingTicket.m_txid, m_nBlock, m_txid));
    }

    // D. Check if this PastelID hasn't changed Ethereum Address in last 24 hours.
    CChangeEthereumAddressTicket _ticket;
    _ticket.pastelID = pastelID;
    const bool bFoundTicketBySecondaryKey = masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(_ticket);
    if (bFoundTicketBySecondaryKey) {
        const unsigned int height = (bPreReg || IsBlock(0)) ? chainHeight : m_nBlock;
        if (height <= _ticket.m_nBlock + 24 * 24) { // For testing purpose, the value 24 * 24 can be lower to decrease the waiting time
            // D.2 IF PastelID has changed Ethereum Address in last 24 hours (~24*24 blocks), do not allow them to change
            throw runtime_error(strprintf("%s ticket is invalid. Already changed in last 24 hours. Ethereum Address - [%s]", GetTicketDescription(TicketID::EthereumAddress), pastelID));
        }
    }

    // E. Check if ticket fee is valid
    const auto expectedFee = bFoundTicketBySecondaryKey ? masterNodeCtrl.MasternodeEthereumAddressChangeAgainFee : masterNodeCtrl.MasternodeEthereumAddressFirstChangeFee;
    if (fee != expectedFee)
        throw runtime_error(strprintf("%s ticket's fee is invalid. PastelID - [%s], invalid fee - [%" PRId64 "], expected fee - [%" PRId64 "]",
                                           GetTicketDescription(TicketID::EthereumAddress), pastelID, fee, expectedFee));
    return true;
}

CChangeEthereumAddressTicket CChangeEthereumAddressTicket::Create(string _pastelID, string _ethereumAddress, SecureString&& strKeyPass)
{
    CChangeEthereumAddressTicket ticket(move(_pastelID), move(_ethereumAddress));

    // Check if PastelID already have an Ethereum Address on the blockchain.
    if (!masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(ticket)) {
        // IF PastelID has no Ethereum Address yet, the fee is 100 PSL
        ticket.fee = masterNodeCtrl.MasternodeEthereumAddressFirstChangeFee;
    } else {
        // IF PastelID changed Ethereum Address before, fee should be 5000
        ticket.fee = masterNodeCtrl.MasternodeEthereumAddressChangeAgainFee;
    }

    ticket.GenerateTimestamp();

    string strTicket = ticket.ToStr();
    ticket.signature = string_to_vector(CPastelID::Sign(strTicket, ticket.pastelID, move(strKeyPass)));

    return ticket;
}

bool CChangeEthereumAddressTicket::FindTicketInDb(const string& key, CChangeEthereumAddressTicket& ticket)
{
    ticket.ethereumAddress = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

bool CChangeEthereumAddressTicket::isEthereumAddressInvalid(const string& ethereumAddress, string& error)
{
    // Check if address is 40 characters long:
    if (ethereumAddress.size() != 40) {
        error = "Invalid length of ethereum address, the length should be exactly 40 characters";
        return true;
    }

    // Check if doesn't start with 0x:
    if ((ethereumAddress.substr(0, 2)) != "0x") {
        error = "Invalid ethereum address, should start with 0x";
        return true;
    }

    // Check if contains characters that is different from valid hex digits:
    if (!all_of(ethereumAddress.begin(), ethereumAddress.end(), [&](unsigned char c) {
            return (isxdigit(c));
        })) {
        error = "Invalid Ethereum address, should only contain hex digits";
        return true;
    }

    return false;
}
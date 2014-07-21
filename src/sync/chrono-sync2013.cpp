/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil -*- */
/**
 * Copyright (C) 2014 Regents of the University of California.
 * @author: Jeff Thompson <jefft0@remap.ucla.edu>
 * Derived from ChronoChat-js by Qiuhan Ding and Wentao Shang.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version, with the additional exemption that
 * compiling, linking, and/or using OpenSSL is allowed.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * A copy of the GNU General Public License is in the file COPYING.
 */

// Only compile if ndn-cpp-config.h defines NDN_CPP_HAVE_PROTOBUF = 1.
#include <ndn-cpp/ndn-cpp-config.h>
#if NDN_CPP_HAVE_PROTOBUF

#include <stdexcept>
#include "../util/logging.hpp"
#include "sync-state.pb.h"
#include "digest-tree.hpp"
#include <ndn-cpp/sync/chrono-sync2013.hpp>

using namespace std;

namespace ndn {

ChronoSync2013::ChronoSync2013
  (const OnReceivedSyncState& onReceivedSyncState,
   const OnInitialized& onInitialized, const Name& applicationDataPrefix,
   const Name& applicationBroadcastPrefix, int sessionNo, Transport& transport,
   Face& face, KeyChain& keyChain, const Name& certificateName,
   Milliseconds syncLifetime, const OnRegisterFailed& onRegisterFailed)
: onReceivedSyncState_(onReceivedSyncState), onInitialized_(onInitialized),
  applicationDataPrefixUri_(applicationDataPrefix.toUri()),
  applicationBroadcastPrefix_(applicationBroadcastPrefix), session_(sessionNo),
  transport_(transport), face_(face), keyChain_(keyChain),
  certificateName_(certificateName), sync_lifetime_(syncLifetime),
  usrseq_(-1), digest_tree_(new DigestTree())
{
  Sync::SyncStateMsg emptyContent;
  digest_log_.push_back(ptr_lib::make_shared<DigestLogEntry>
    ("00", emptyContent.ss()));

  face.registerPrefix
    (applicationBroadcastPrefix_,
     bind(&ChronoSync2013::onInterest, this, _1, _2, _3, _4), onRegisterFailed);

  Interest interest(applicationBroadcastPrefix_);
  interest.getName().append("00");
  interest.setInterestLifetimeMilliseconds(1000);
  interest.setAnswerOriginKind(ndn_Interest_ANSWER_NO_CONTENT_STORE);
  face.expressInterest
    (interest, bind(&ChronoSync2013::onData, this, _1, _2),
     bind(&ChronoSync2013::initialTimeOut, this, _1));
  _LOG_DEBUG("initial sync expressed");
  _LOG_DEBUG(interest.getName().toUri());
}

int
ChronoSync2013::logfind(const std::string& digest) const
{
  for (size_t i = 0; i < digest_log_.size(); ++i) {
    if (digest == digest_log_[i]->getDigest())
      return i;
  }

  return -1;
};

bool
ChronoSync2013::update
  (const google::protobuf::RepeatedPtrField<Sync::SyncState >& content)
{
  for (size_t i = 0; i < content.size(); ++i) {
    if (content.Get(i).type() == 0) {
      if (digest_tree_->update
          (content.Get(i).name(), content.Get(i).seqno().session(),
           content.Get(i).seqno().seq())) {
        // The digest tree was updated.
        if (applicationDataPrefixUri_ == content.Get(i).name())
          usrseq_ = content.Get(i).seqno().seq();
      }
    }
  }

  if (logfind(digest_tree_->getRoot()) == -1) {
    digest_log_.push_back(ptr_lib::make_shared<DigestLogEntry>
      (digest_tree_->getRoot(), content));
    return true;
  }
  else
    return false;
}

int
ChronoSync2013::getProducerSequenceNo(const std::string& dataPrefix, int sessionNo) const
{
  int index = digest_tree_->find(dataPrefix, sessionNo);
  if (index < 0)
    return -1;
  else
    return digest_tree_->get(index).getSequenceNo();
}

void
ChronoSync2013::publishNextSequenceNo()
{
  ++usrseq_;

  Sync::SyncStateMsg syncMessage;
  Sync::SyncState* content = syncMessage.add_ss();
  content->set_name(applicationDataPrefixUri_);
  content->set_type(Sync::SyncState_ActionType_UPDATE);
  content->mutable_seqno()->set_seq(usrseq_);
  content->mutable_seqno()->set_session(session_);

  broadcastSyncState(digest_tree_->getRoot(), syncMessage);

  if (!update(syncMessage.ss()))
    // Since we incremented the sequence number, we expect there to be a
    //   new digest log entry.
    throw runtime_error
      ("ChronoSync: update did not create a new digest log entry");

  // TODO: Should we have an option to not express an interest if this is the
  //   final publish of the session?
  Interest interest(applicationBroadcastPrefix_);
  interest.getName().append(digest_tree_->getRoot());
  interest.setInterestLifetimeMilliseconds(sync_lifetime_);
  face_.expressInterest
    (interest, bind(&ChronoSync2013::onData, this, _1, _2),
     bind(&ChronoSync2013::syncTimeout, this, _1));
}

void
ChronoSync2013::onInterest
  (const ptr_lib::shared_ptr<const Name>& prefix,
   const ptr_lib::shared_ptr<const Interest>& inst, Transport& transport,
   uint64_t registerPrefixId)
{
  // Search if the digest already exists in the digest log.
   _LOG_DEBUG("Sync Interest received in callback.");
   _LOG_DEBUG(inst->getName().toUri());

  string syncdigest = inst->getName().get
    (applicationBroadcastPrefix_.size()).toEscapedString();
  if (inst->getName().size() == applicationBroadcastPrefix_.size() + 2)
    // Assume this is a recovery interest.
    syncdigest = inst->getName().get
      (applicationBroadcastPrefix_.size() + 1).toEscapedString();
   _LOG_DEBUG("syncdigest: " + syncdigest);
  if (inst->getName().size() == applicationBroadcastPrefix_.size() + 2 ||
      syncdigest == "00")
    // Recovery interest or newcomer interest.
    processRecoveryInst(*inst, syncdigest, transport);
  else {
    if (syncdigest != digest_tree_->getRoot()) {
      size_t index = logfind(syncdigest);
      if (index == -1) {
        ChronoSync2013& self = *this;
#if 0 // TODO: Set the timeout without using usleep.
        //Wait 2 seconds to see whether there is any data packet coming back
        setTimeout(function(){self.judgeRecovery(syncdigest, transport);},2000);
        _LOG_DEBUG("set timer recover");
#else
        _LOG_DEBUG("set timer recover");
        usleep(1000000);
        judgeRecovery(syncdigest, transport);
#endif
      }
      else
        // common interest processing
        processSyncInst(index, syncdigest, transport);
    }
  }
}

void
ChronoSync2013::onData
  (const ptr_lib::shared_ptr<const Interest>& inst,
   const ptr_lib::shared_ptr<Data>& co)
{
  _LOG_DEBUG("Sync ContentObject received in callback");
  _LOG_DEBUG("name: " + co->getName().toUri());
  Sync::SyncStateMsg content_t;
  content_t.ParseFromArray(co->getContent().buf(), co->getContent().size());
  const google::protobuf::RepeatedPtrField<Sync::SyncState >&content = content_t.ss();
  bool isRecovery;
  if (digest_tree_->getRoot() == "00") {
    isRecovery = true;
    //processing initial sync data
    initialOndata(content);
  }
  else {
    update(content);
    if (inst->getName().size() == applicationBroadcastPrefix_.size() + 2)
      // Assume this is a recovery interest.
      isRecovery = true;
    else
      isRecovery = false;
  }

  // Send the interests to fetch the application data.
  vector<SyncState> syncStates;
  for (size_t i = 0; i < content.size(); ++i) {
    // Only report UPDATE sync states.
    if (content.Get(i).type() == 0)
      syncStates.push_back(SyncState
        (content.Get(i).name(), content.Get(i).seqno().session(),
         content.Get(i).seqno().seq()));
  }
  onReceivedSyncState_(syncStates, isRecovery);

  Name n(applicationBroadcastPrefix_);
  n.append(digest_tree_->getRoot());
  Interest interest(n);
  interest.setInterestLifetimeMilliseconds(sync_lifetime_);
  face_.expressInterest
    (interest, bind(&ChronoSync2013::onData, this, _1, _2),
     bind(&ChronoSync2013::syncTimeout, this, _1));
  _LOG_DEBUG("Syncinterest expressed:");
  _LOG_DEBUG(n.toUri());
}

void
ChronoSync2013::processRecoveryInst
  (const Interest& inst, const string& syncdigest, Transport& transport)
{
  _LOG_DEBUG("processRecoveryInst");
  if (logfind(syncdigest) != -1) {
    Sync::SyncStateMsg content_t;
    for (size_t i = 0; i < digest_tree_->size(); ++i) {
      Sync::SyncState* content = content_t.add_ss();
      content->set_name(digest_tree_->get(i).getDataPrefix());
      content->set_type(Sync::SyncState_ActionType_UPDATE);
      content->mutable_seqno()->set_seq(digest_tree_->get(i).getSequenceNo());
      content->mutable_seqno()->set_session(digest_tree_->get(i).getSessionNo());
    }

    if (content_t.ss_size() != 0) {
      ptr_lib::shared_ptr<vector<uint8_t> > array(new vector<uint8_t>(content_t.ByteSize()));
      content_t.SerializeToArray(&array->front(), array->size());
      Data co(inst.getName());
      co.setContent(Blob(array, false));
      keyChain_.sign(co, certificateName_);
      try {
        transport.send(*co.wireEncode());
        _LOG_DEBUG("send recovery data back");
        _LOG_DEBUG(inst.getName().toUri());
      }
      catch (std::exception& e) {
        _LOG_DEBUG(e.what());
      }
    }
  }
}

void
ChronoSync2013::processSyncInst
  (int index, const string& syncdigest_t, Transport& transport)
{
  vector<string> data_name;
  vector<int> data_seq;
  vector<int> data_ses;
  for (size_t j = index + 1; j < digest_log_.size(); ++j) {
    const google::protobuf::RepeatedPtrField<Sync::SyncState>& temp =
      digest_log_[j]->getData();
    for (size_t i = 0; i < temp.size(); ++i) {
      if (temp.Get(i).type() != 0)
        continue;

      if (digest_tree_->find(temp.Get(i).name(), temp.Get(i).seqno().session()) != -1) {
        int n = -1;
        for (size_t k = 0; k < data_name.size(); ++k) {
          if (data_name[k] == temp.Get(i).name()) {
            n = k;
            break;
          }
        }
        if (n == -1) {
          data_name.push_back(temp.Get(i).name());
          data_seq.push_back(temp.Get(i).seqno().seq());
          data_ses.push_back(temp.Get(i).seqno().session());
        }
        else {
          data_seq[n] = temp.Get(i).seqno().seq();
          data_ses[n] = temp.Get(i).seqno().session();
        }
      }
    }
  }

  Sync::SyncStateMsg content_t;
  for (size_t i = 0; i < data_name.size(); ++i) {
    Sync::SyncState* content = content_t.add_ss();
    content->set_name(data_name[i]);
    content->set_type(Sync::SyncState_ActionType_UPDATE);
    content->mutable_seqno()->set_seq(data_seq[i]);
    content->mutable_seqno()->set_session(data_ses[i]);
  }

  if (content_t.ss_size() != 0) {
    Name n(applicationBroadcastPrefix_);
    n.append(syncdigest_t);
    ptr_lib::shared_ptr<vector<uint8_t> > array(new vector<uint8_t>(content_t.ByteSize()));
    content_t.SerializeToArray(&array->front(), array->size());
    Data co(n);
    co.setContent(Blob(array, false));
    keyChain_.sign(co, certificateName_);
    try {
      transport.send(*co.wireEncode());
      _LOG_DEBUG("Sync Data send");
      _LOG_DEBUG(n.toUri());
    } catch (std::exception& e) {
      _LOG_DEBUG(e.what());
    }
  }
}

void
ChronoSync2013::sendRecovery(const string& syncdigest_t)
{
  _LOG_DEBUG("unknown digest: ");
  Name n(applicationBroadcastPrefix_);
  n.append("recovery").append(syncdigest_t);
  Interest interest(n);
  interest.setInterestLifetimeMilliseconds(sync_lifetime_);
  face_.expressInterest
    (interest, bind(&ChronoSync2013::onData, this, _1, _2),
     bind(&ChronoSync2013::syncTimeout, this, _1));
  _LOG_DEBUG("Recovery Syncinterest expressed:");
  _LOG_DEBUG(n.toUri());
}

void
ChronoSync2013::judgeRecovery(const string& syncdigest_t, Transport& transport)
{
  int index2 = logfind(syncdigest_t);
  if (index2 != -1) {
    if (syncdigest_t != digest_tree_->getRoot())
      processSyncInst(index2, syncdigest_t, transport);
  }
  else
    sendRecovery(syncdigest_t);
}

void
ChronoSync2013::syncTimeout(const ptr_lib::shared_ptr<const Interest>& interest)
{
   _LOG_DEBUG("Sync Interest time out.");
   _LOG_DEBUG("Sync Interest name: " + interest->getName().toUri());
  string component = interest->getName().get(4).toEscapedString();
  if (component == digest_tree_->getRoot()) {
    Name n(interest->getName());
    Interest retryInterest(interest->getName());
    retryInterest.setInterestLifetimeMilliseconds(sync_lifetime_);
    face_.expressInterest
      (retryInterest, bind(&ChronoSync2013::onData, this, _1, _2),
       bind(&ChronoSync2013::syncTimeout, this, _1));
     _LOG_DEBUG("Syncinterest expressed:");
     _LOG_DEBUG(n.toUri());
  }
}

void
ChronoSync2013::initialOndata
  (const google::protobuf::RepeatedPtrField<Sync::SyncState >& content)
{
  // The user is a new comer and receive data of all other people in the group.
  update(content);
  string digest_t = digest_tree_->getRoot();
  for (size_t i = 0; i < content.size(); ++i) {
    if (content.Get(i).name() == applicationDataPrefixUri_ && content.Get(i).seqno().session() == session_) {
      // If the user was an old comer, after add the static log he needs to increase his seqno by 1.
      Sync::SyncStateMsg content_t;
      Sync::SyncState* content2 = content_t.add_ss();
      content2->set_name(applicationDataPrefixUri_);
      content2->set_type(Sync::SyncState_ActionType_UPDATE);
      content2->mutable_seqno()->set_seq(content.Get(i).seqno().seq() + 1);
      content2->mutable_seqno()->set_session(session_);
      if (update(content_t.ss()))
        onInitialized_();
    }
  }

  Sync::SyncStateMsg content2_t;
  if (usrseq_ >= 0) {
    // Send the data packet with the new seqno back.
    Sync::SyncState* content2 = content2_t.add_ss();
    content2->set_name(applicationDataPrefixUri_);
    content2->set_type(Sync::SyncState_ActionType_UPDATE);
    content2->mutable_seqno()->set_seq(usrseq_);
    content2->mutable_seqno()->set_session(session_);
  }
  else {
    Sync::SyncState* content2 = content2_t.add_ss();
    content2->set_name(applicationDataPrefixUri_);
    content2->set_type(Sync::SyncState_ActionType_UPDATE);
    content2->mutable_seqno()->set_seq(0);
    content2->mutable_seqno()->set_session(session_);
  }

  broadcastSyncState(digest_t, content2_t);

  if (digest_tree_->find(applicationDataPrefixUri_, session_) == -1) {
    // the user hasn't put himself in the digest tree.
    _LOG_DEBUG("initial state")
    ++usrseq_;
    Sync::SyncStateMsg content_t;
    Sync::SyncState* content2 = content_t.add_ss();
    content2->set_name(applicationDataPrefixUri_);
    content2->set_type(Sync::SyncState_ActionType_UPDATE);
    content2->mutable_seqno()->set_seq(usrseq_);
    content2->mutable_seqno()->set_session(session_);

    if (update(content_t.ss()))
      onInitialized_();
  }
}

void
ChronoSync2013::initialTimeOut(const ptr_lib::shared_ptr<const Interest>& interest)
{
  _LOG_DEBUG("initial sync timeout");
  _LOG_DEBUG("no other people");
  ++usrseq_;
  if (usrseq_ != 0)
    // Since there were no other users, we expect sequence no 0.
    throw runtime_error
      ("ChronoSync: usrseq_ is not the expected value of 0 for first use.");

  Sync::SyncStateMsg content_t;
  Sync::SyncState* content = content_t.add_ss();
  content->set_name(applicationDataPrefixUri_);
  content->set_type(Sync::SyncState_ActionType_UPDATE);
  content->mutable_seqno()->set_seq(usrseq_);
  content->mutable_seqno()->set_session(session_);
  update(content_t.ss());

  onInitialized_();

  Name n(applicationBroadcastPrefix_);
  n.append(digest_tree_->getRoot());
  Interest retryInterest(n);
  retryInterest.setInterestLifetimeMilliseconds(sync_lifetime_);
  face_.expressInterest
    (retryInterest, bind(&ChronoSync2013::onData, this, _1, _2),
     bind(&ChronoSync2013::syncTimeout, this, _1));
  _LOG_DEBUG("Syncinterest expressed:");
  _LOG_DEBUG(n.toUri());
}

void
ChronoSync2013::broadcastSyncState
  (const string& digest, const Sync::SyncStateMsg& syncMessage)
{
  ptr_lib::shared_ptr<vector<uint8_t> > array(new vector<uint8_t>(syncMessage.ByteSize()));
  syncMessage.SerializeToArray(&array->front(), array->size());
  Data data(applicationBroadcastPrefix_);
  data.getName().append(digest);
  data.setContent(Blob(array, false));
  keyChain_.sign(data, certificateName_);
  try {
    // TODO: Should use a MemoryContentCache instead of a direct poke.
    transport_.send(*data.wireEncode());
  } catch (std::exception& e) {
    _LOG_DEBUG(e.what());
  }
}

ChronoSync2013::DigestLogEntry::DigestLogEntry
  (const std::string& digest,
   const google::protobuf::RepeatedPtrField<Sync::SyncState>& data)
  : digest_(digest),
   data_(new google::protobuf::RepeatedPtrField<Sync::SyncState>(data))
{
}

}

#endif // NDN_CPP_HAVE_PROTOBUF
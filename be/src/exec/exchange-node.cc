// Copyright (c) 2012 Cloudera, Inc. All rights reserved.

#include "exec/exchange-node.h"

#include <boost/scoped_ptr.hpp>

#include "runtime/data-stream-mgr.h"
#include "runtime/runtime-state.h"
#include "runtime/row-batch.h"
#include "util/debug-util.h"
#include "util/runtime-profile.h"
#include "gen-cpp/PlanNodes_types.h"

using namespace impala;
using namespace std;
using namespace boost;

ExchangeNode::ExchangeNode(
    ObjectPool* pool, const TPlanNode& tnode, const DescriptorTbl& descs)
  : ExecNode(pool, tnode, descs),
    num_senders_(0),
    stream_recvr_(NULL) {
}

Status ExchangeNode::Prepare(RuntimeState* state) {
  RETURN_IF_ERROR(ExecNode::Prepare(state));
  
  // TODO: figure out appropriate buffer size
  // row descriptor of this node and the incoming stream should be the same.
  DCHECK_GT(num_senders_, 0);
  stream_recvr_.reset(state->stream_mgr()->CreateRecvr(
    row_descriptor_, state->fragment_instance_id(), id_, num_senders_, 1024 * 1024));
  return Status::OK;
}

Status ExchangeNode::Open(RuntimeState* state) {
  SCOPED_TIMER(runtime_profile_->total_time_counter());
  return Status::OK;
}

Status ExchangeNode::GetNext(RuntimeState* state, RowBatch* output_batch, bool* eos) {
  SCOPED_TIMER(runtime_profile_->total_time_counter());
  bool is_cancelled;
  scoped_ptr<RowBatch> input_batch(stream_recvr_->GetBatch(&is_cancelled));
  VLOG_FILE << "exch: has batch=" << (input_batch.get() == NULL ? "false" : "true")
            << " #rows=" << (input_batch.get() != NULL ? input_batch->num_rows() : 0)
            << " is_cancelled=" << (is_cancelled ? "true" : "false")
            << " instance_id=" << state->fragment_instance_id();
  if (is_cancelled) return Status(TStatusCode::CANCELLED);
  output_batch->Reset();
  *eos = (input_batch.get() == NULL);
  if (*eos) return Status::OK;

  // We assume that we can always move the entire input batch into the output batch
  // (if that weren't the case, the code would be more complicated).
  DCHECK_GE(output_batch->capacity(), input_batch->capacity());

  // copy all rows (up to limit) and attach all mempools from the input batch
  DCHECK(input_batch->row_desc().IsPrefixOf(output_batch->row_desc()));
  int i = 0;
  for (; i < input_batch->num_rows(); ++i) {
    if (ReachedLimit()) {
      *eos = true;
      break;
    }
    TupleRow* src = input_batch->GetRow(i);
    int j = output_batch->AddRow();
    DCHECK_EQ(i, j);
    TupleRow* dest = output_batch->GetRow(i);
    // this works as expected if rows from input_batch form a prefix of
    // rows in output_batch
    input_batch->CopyRow(src, dest);
    output_batch->CommitLastRow();
  }
  num_rows_returned_ += i;
  COUNTER_SET(rows_returned_counter_, num_rows_returned_);
  input_batch->TransferResourceOwnership(output_batch);
  return Status::OK;
}

void ExchangeNode::DebugString(int indentation_level, std::stringstream* out) const {
  *out << string(indentation_level * 2, ' ');
  *out << "ExchangeNode(#senders=" << num_senders_;
  ExecNode::DebugString(indentation_level, out);
  *out << ")";
}

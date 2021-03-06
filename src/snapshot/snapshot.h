// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_SNAPSHOT_H_
#define V8_SNAPSHOT_SNAPSHOT_H_

#include "src/snapshot/partial-serializer.h"
#include "src/snapshot/startup-serializer.h"

#include "src/utils.h"

namespace v8 {
namespace internal {

// Forward declarations.
class Isolate;
class BuiltinSerializer;
class PartialSerializer;
class StartupSerializer;

// Wrapper around reservation sizes and the serialization payload.
class SnapshotData : public SerializedData {
 public:
  // Used when producing.
  template <class AllocatorT>
  explicit SnapshotData(const Serializer<AllocatorT>* serializer);

  // Used when consuming.
  explicit SnapshotData(const Vector<const byte> snapshot)
      : SerializedData(const_cast<byte*>(snapshot.begin()), snapshot.length()) {
  }

  std::vector<Reservation> Reservations() const;
  virtual Vector<const byte> Payload() const;

  Vector<const byte> RawData() const {
    return Vector<const byte>(data_, size_);
  }

 protected:
  // The data header consists of uint32_t-sized entries:
  // [0] magic number and (internal) external reference count
  // [1] number of reservation size entries
  // [2] payload length
  // ... reservations
  // ... serialized payload
  static const uint32_t kNumReservationsOffset =
      kMagicNumberOffset + kUInt32Size;
  static const uint32_t kPayloadLengthOffset =
      kNumReservationsOffset + kUInt32Size;
  static const uint32_t kHeaderSize = kPayloadLengthOffset + kUInt32Size;
};

class BuiltinSnapshotData final : public SnapshotData {
 public:
  // Used when producing.
  // This simply forwards to the SnapshotData constructor.
  // The BuiltinSerializer appends the builtin offset table to the payload.
  explicit BuiltinSnapshotData(const BuiltinSerializer* serializer);

  // Used when consuming.
  explicit BuiltinSnapshotData(const Vector<const byte> snapshot)
      : SnapshotData(snapshot) {
  }

  // Returns the serialized payload without the builtin offsets table.
  Vector<const byte> Payload() const override;

  // Returns only the builtin offsets table.
  Vector<const uint32_t> BuiltinOffsets() const;

 private:
  // In addition to the format specified in SnapshotData, BuiltinsSnapshotData
  // includes a list of builtin at the end of the serialized payload:
  //
  // ...
  // ... serialized payload
  // ... list of builtins offsets
};

#ifdef V8_EMBEDDED_BUILTINS
class EmbeddedData final {
 public:
  static EmbeddedData FromIsolate(Isolate* isolate);
  static EmbeddedData FromBlob();

  const uint8_t* data() const { return data_; }
  uint32_t size() const { return size_; }

  void Dispose() { delete[] data_; }

  Address InstructionStartOfBuiltin(int i) const;
  uint32_t InstructionSizeOfBuiltin(int i) const;

  bool ContainsBuiltin(int i) const { return InstructionSizeOfBuiltin(i) > 0; }

  // Padded with kCodeAlignment.
  uint32_t PaddedInstructionSizeOfBuiltin(int i) const {
    return RoundUp<kCodeAlignment>(InstructionSizeOfBuiltin(i));
  }

  size_t CreateHash() const;
  size_t Hash() const {
    return *reinterpret_cast<const size_t*>(data_ + HashOffset());
  }

  // The layout of the blob is as follows:
  //
  // [0]     hash of the remaining blob
  // [1]     offset of instruction stream 0
  // ...     offsets
  // [N + 1] length of instruction stream 0
  // ...     lengths
  // ...     instruction streams

  static constexpr uint32_t kTableSize = Builtins::builtin_count;
  static constexpr uint32_t HashOffset() { return 0; }
  static constexpr uint32_t HashSize() { return kSizetSize; }
  static constexpr uint32_t OffsetsOffset() {
    return HashOffset() + HashSize();
  }
  static constexpr uint32_t OffsetsSize() { return kUInt32Size * kTableSize; }
  static constexpr uint32_t LengthsOffset() {
    return OffsetsOffset() + OffsetsSize();
  }
  static constexpr uint32_t LengthsSize() { return kUInt32Size * kTableSize; }
  static constexpr uint32_t RawDataOffset() {
    return RoundUp<kCodeAlignment>(LengthsOffset() + LengthsSize());
  }

 private:
  EmbeddedData(const uint8_t* data, uint32_t size) : data_(data), size_(size) {}

  const uint32_t* Offsets() const {
    return reinterpret_cast<const uint32_t*>(data_ + OffsetsOffset());
  }
  const uint32_t* Lengths() const {
    return reinterpret_cast<const uint32_t*>(data_ + LengthsOffset());
  }
  const uint8_t* RawData() const { return data_ + RawDataOffset(); }

  const uint8_t* data_;
  uint32_t size_;
};
#endif

class Snapshot : public AllStatic {
 public:
  // ---------------- Deserialization ----------------

  // Initialize the Isolate from the internal snapshot. Returns false if no
  // snapshot could be found.
  static bool Initialize(Isolate* isolate);

  // Create a new context using the internal partial snapshot.
  static MaybeHandle<Context> NewContextFromSnapshot(
      Isolate* isolate, Handle<JSGlobalProxy> global_proxy,
      size_t context_index,
      v8::DeserializeEmbedderFieldsCallback embedder_fields_deserializer);

  // Deserializes a single given builtin code object. Intended to be called at
  // runtime after the isolate (and the builtins table) has been fully
  // initialized.
  static Code* DeserializeBuiltin(Isolate* isolate, int builtin_id);
  static void EnsureAllBuiltinsAreDeserialized(Isolate* isolate);
  static Code* EnsureBuiltinIsDeserialized(Isolate* isolate,
                                           Handle<SharedFunctionInfo> shared);

  // Deserializes a single given handler code object. Intended to be called at
  // runtime after the isolate has been fully initialized.
  static Code* DeserializeHandler(Isolate* isolate,
                                  interpreter::Bytecode bytecode,
                                  interpreter::OperandScale operand_scale);

  // ---------------- Helper methods ----------------

  static bool HasContextSnapshot(Isolate* isolate, size_t index);
  static bool EmbedsScript(Isolate* isolate);

  // To be implemented by the snapshot source.
  static const v8::StartupData* DefaultSnapshotBlob();

  // ---------------- Serialization ----------------

  static v8::StartupData CreateSnapshotBlob(
      const SnapshotData* startup_snapshot,
      const BuiltinSnapshotData* builtin_snapshot,
      const std::vector<SnapshotData*>& context_snapshots,
      bool can_be_rehashed);

#ifdef DEBUG
  static bool SnapshotIsValid(const v8::StartupData* snapshot_blob);
#endif  // DEBUG

 private:
  static uint32_t ExtractNumContexts(const v8::StartupData* data);
  static uint32_t ExtractContextOffset(const v8::StartupData* data,
                                       uint32_t index);
  static bool ExtractRehashability(const v8::StartupData* data);
  static Vector<const byte> ExtractStartupData(const v8::StartupData* data);
  static Vector<const byte> ExtractBuiltinData(const v8::StartupData* data);
  static Vector<const byte> ExtractContextData(const v8::StartupData* data,
                                               uint32_t index);

  static uint32_t GetHeaderValue(const v8::StartupData* data, uint32_t offset) {
    return ReadLittleEndianValue<uint32_t>(
        reinterpret_cast<Address>(data->data) + offset);
  }
  static void SetHeaderValue(char* data, uint32_t offset, uint32_t value) {
    WriteLittleEndianValue(reinterpret_cast<Address>(data) + offset, value);
  }

  static void CheckVersion(const v8::StartupData* data);

  // Snapshot blob layout:
  // [0] number of contexts N
  // [1] rehashability
  // [2] (128 bytes) version string
  // [3] offset to builtins
  // [4] offset to context 0
  // [5] offset to context 1
  // ...
  // ... offset to context N - 1
  // ... startup snapshot data
  // ... builtin snapshot data
  // ... context 0 snapshot data
  // ... context 1 snapshot data

  static const uint32_t kNumberOfContextsOffset = 0;
  // TODO(yangguo): generalize rehashing, and remove this flag.
  static const uint32_t kRehashabilityOffset =
      kNumberOfContextsOffset + kUInt32Size;
  static const uint32_t kVersionStringOffset =
      kRehashabilityOffset + kUInt32Size;
  static const uint32_t kVersionStringLength = 64;
  static const uint32_t kBuiltinOffsetOffset =
      kVersionStringOffset + kVersionStringLength;
  static const uint32_t kFirstContextOffsetOffset =
      kBuiltinOffsetOffset + kUInt32Size;

  static uint32_t StartupSnapshotOffset(int num_contexts) {
    return kFirstContextOffsetOffset + num_contexts * kInt32Size;
  }

  static uint32_t ContextSnapshotOffsetOffset(int index) {
    return kFirstContextOffsetOffset + index * kInt32Size;
  }

  DISALLOW_IMPLICIT_CONSTRUCTORS(Snapshot);
};

#ifdef V8_USE_EXTERNAL_STARTUP_DATA
void SetSnapshotFromFile(StartupData* snapshot_blob);
#endif

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_SNAPSHOT_H_

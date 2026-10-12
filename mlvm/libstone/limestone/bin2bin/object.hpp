#pragma once
#include "metacode/metacode.hpp"
#include <map>

namespace limestone::bin2bin {
enum class ByteOrder { Little, Big };
enum class ElfClass { Elf32=1, Elf64=2 };
struct ObjectFormat {
  uint16_t machine=0;
  ByteOrder byte_order=ByteOrder::Little;
  uint32_t flags=0;
  uint8_t osabi=0, abi_version=0;
  ElfClass elf_class=ElfClass::Elf64;
  bool operator==(const ObjectFormat&) const = default;
};
inline constexpr uint32_t object_undefined=UINT32_MAX, object_absolute=UINT32_MAX-1;
struct ObjectSection {
  std::string name;
  uint32_t type=1; // ELF PROGBITS (1), NOTE (7) or NOBITS (8).
  uint64_t flags=0, alignment=1, entry_size=0;
  std::vector<uint8_t> bytes;
  uint64_t zero_fill=0;
  uint64_t size() const { return type==8?zero_fill:bytes.size(); }
};
enum class SymbolBinding { Local, Global, Weak };
struct ObjectSymbol {
  std::string name;
  uint32_t section=object_undefined;
  uint64_t value=0, size=0;
  SymbolBinding binding=SymbolBinding::Global;
  uint8_t type=0, visibility=0;
};
struct ObjectRelocation {
  uint32_t section=0, symbol=0, type=0;
  uint64_t offset=0;
  int64_t addend=0;
  bool implicit_addend=false;
};
struct ObjectGroupMember {
  enum class Kind { Content, RELA, REL } kind=Kind::Content;
  uint32_t section=0;
  bool operator==(const ObjectGroupMember&) const = default;
};
struct ObjectGroup {
  uint32_t signature=0, flags=1; // ELF GRP_COMDAT; zero preserves an ordinary group.
  std::vector<ObjectGroupMember> members;
  std::string name=".group";
};
struct ObjectFile {
  ObjectFormat format;
  std::vector<ObjectSection> sections;
  std::vector<ObjectSymbol> symbols;
  std::vector<ObjectRelocation> relocations;
  std::string source;
  std::vector<ObjectGroup> groups;
};
struct ObjectLimits {
  uint64_t bytes=64*1024*1024;
  size_t sections=16384, symbols=1048576, relocations=1048576, groups=4096;
};
enum class RelocationKind { Absolute, PCRelative, GOTAbsolute, GOTPCRelative,
  GOTBaseRelative, PLTPCRelative, TLSOffset, TLSModule, Custom };
struct RelocationType {
  uint32_t type=0;
  std::string name;
  RelocationKind kind=RelocationKind::Absolute;
  uint32_t storage_bytes=0, bit_offset=0, bits=0;
  uint64_t scale=1;
  bool signed_value=false;
  int64_t pc_bias=0;
  std::optional<bool> implicit_addend_signed;
  std::string adapter;
};
struct GOTContract {uint32_t entry_bytes=0;uint64_t alignment=0;};
struct PLTContract {std::string adapter;uint32_t entry_bytes=0;uint64_t alignment=0;};
struct ObjectTarget {
  ObjectFormat format;
  uint64_t text_alignment=1;
  std::map<uint32_t,RelocationType> relocations;
  std::string architecture;
  // Explicit byte-preserving OS/processor section contracts. Indexed contents
  // and special allocation semantics need a separate adapter.
  std::vector<uint32_t> opaque_section_types;
  std::optional<GOTContract> got;
  std::optional<PLTContract> plt;
  bool tls=false; // Explicit permission for an independently supplied TLS layout.
};
// Authoritative ELF identity and relocation encodings come from tooling.object_file.
Result<ObjectTarget> object_target(const metacode::Architecture&);
Result<int> validate(const ObjectTarget&);
Result<int> validate(const ObjectFile&,const ObjectLimits& = {});
// Owning ELF32/ELF64 ET_REL ingestion and deterministic REL/RELA emission. No host structs
// are overlaid on input bytes; both byte orders use the same checked decoder.
Result<ObjectFile> load_elf(std::span<const uint8_t>,std::string_view source={},const ObjectLimits& = {});
Result<std::vector<uint8_t>> emit_elf(const ObjectFile&,const ObjectLimits& = {});
struct ArchiveMember { std::string name; ObjectFile object; };
struct ObjectArchive { std::vector<ArchiveMember> members; std::string source; };
struct ArchiveLimits { uint64_t bytes=64*1024*1024; size_t members=4096; ObjectLimits object; };
// Owning regular ar archives, including GNU/BSD long names and symbol tables.
Result<ObjectArchive> load_archive(std::span<const uint8_t>,std::string_view source={},const ArchiveLimits& = {});
Result<std::vector<uint8_t>> emit_archive(const ObjectArchive&,const ArchiveLimits& = {});
struct NamedRelocation { uint64_t offset; std::string kind, symbol; int64_t addend=0; };
Result<ObjectFile> code_object(const ObjectTarget&,std::span<const uint8_t>,std::string_view symbol,
                              std::span<const NamedRelocation> = {});
struct LinkedSection {
  size_t object; uint32_t section; std::string name;
  uint64_t address, offset, size, flags;
};
struct LinkedTLS {
  uint64_t module=0,alignment=1;
  int64_t thread_pointer_offset=0;
  std::vector<uint8_t> bytes;
  std::map<std::string,uint64_t> symbols; // Offsets in the owning per-thread image.
};
struct LinkedImage {
  uint64_t base_address=0;
  std::vector<uint8_t> bytes;
  std::vector<LinkedSection> sections;
  std::map<std::string,uint64_t> symbols;
  std::map<std::string,uint64_t> got,plt;
  std::optional<LinkedTLS> tls;
  // Providers/libraries remain alive for every address installed in this image.
  std::vector<std::shared_ptr<const void>> owners;
  std::vector<std::string> discarded_groups;
};
struct TLSReference {uint64_t module=0,offset=0;int64_t thread_pointer_offset=0;};
struct DynamicSymbol {uint64_t address=0;std::shared_ptr<const void> owner;std::optional<TLSReference> tls;};
struct TLSContract {uint64_t module=0;int64_t thread_pointer_offset=0;};
struct PLTAdapter {
  std::string identity;
  std::function<Result<std::vector<uint8_t>>(uint64_t entry,uint64_t got)> emit;
  std::function<Result<int>(uint64_t entry,uint64_t got,std::span<const uint8_t>)> verify;
};
struct RelocationPatch {uint32_t offset=0;std::vector<uint8_t> bytes,mask;};
struct RelocationContext {
  const ObjectFile& object;
  const ObjectRelocation& relocation;
  const RelocationType& type;
  uint64_t place=0,symbol=0;
  // Original unpatched storage, including any paired relocation records in
  // object. Patches are relative to this metadata-declared bounded window.
  std::span<const uint8_t> storage;
  const LinkedImage& image;
  std::optional<TLSReference> tls_symbol;
};
struct RelocationAdapter {
  std::string identity;
  std::function<Result<std::vector<RelocationPatch>>(const RelocationContext&)> apply;
  std::function<Result<int>(const RelocationContext&,std::span<const RelocationPatch>)> verify;
};
struct LinkOptions {
  uint64_t base_address=0, max_size=64*1024*1024;
  std::map<std::string,uint64_t> externals;
  // Missing weak symbols resolve to zero. A found dynamic symbol must retain
  // its provider; hidden/internal undefined symbols cannot use a provider.
  std::function<Result<DynamicSymbol>(const ObjectSymbol&)> resolve_dynamic;
  std::optional<TLSContract> tls;
  std::optional<PLTAdapter> plt;
  std::map<std::string,RelocationAdapter> relocation_adapters;
  size_t work_limit=1000000;
  uint64_t input_limit=64*1024*1024;
};
// Layout follows input/section declaration order. The image owns bytes and has no
// executable mapping. Symbol addresses use the caller's explicit installation base.
Result<LinkedImage> link_objects(std::span<const ObjectFile>,const ObjectTarget&,const LinkOptions& = {});
Result<LinkedImage> link_objects(std::span<const ObjectFile* const>,const ObjectTarget&,const LinkOptions& = {});
// Extract members on demand, rescanning in declaration order to a fixed point.
// Undefined weak references do not cause extraction. Externals satisfy demand.
Result<LinkedImage> link_archives(std::span<const ObjectFile>,std::span<const ObjectArchive>,const ObjectTarget&,const LinkOptions& = {});
std::string print_object(const ObjectFile&);
}

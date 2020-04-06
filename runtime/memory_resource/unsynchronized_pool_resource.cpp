#include "runtime/memory_resource/unsynchronized_pool_resource.h"

#include "common/wrappers/likely.h"

#include "runtime/memory_resource/details/memory_ordered_chunk_list.h"

namespace memory_resource {

constexpr size_type unsynchronized_pool_resource::MAX_CHUNK_BLOCK_SIZE_;

void unsynchronized_pool_resource::init(void *buffer, size_type buffer_size) noexcept {
  monotonic_buffer_resource::init(buffer, buffer_size);

  huge_pieces_.hard_reset();
  fallback_resource_.init(nullptr, 0);
  free_chunks_.fill(details::unsynchronized_memory_chunk_list{});
}

void unsynchronized_pool_resource::perform_defragmentation() noexcept {
  memory_debug("perform memory defragmentation\n");
  details::memory_ordered_chunk_list mem_list{memory_begin_};

  huge_pieces_.flush_to(mem_list);
  if (const size_type fallback_resource_left_size = fallback_resource_.size()) {
    mem_list.add_memory(fallback_resource_.memory_current(), fallback_resource_left_size);
    fallback_resource_.init(nullptr, 0);
  }

  // chunk_id == 0 ignored, because it always empty and unused
  php_assert(free_chunks_[0].get_mem() == nullptr);
  for (size_type chunk_id = 1; chunk_id < free_chunks_.size(); ++chunk_id) {
    const size_type chunk_size = details::get_chunk_size(chunk_id);
    for (void *slot_mem = free_chunks_[chunk_id].get_mem(); slot_mem; slot_mem = free_chunks_[chunk_id].get_mem()) {
      mem_list.add_memory(slot_mem, chunk_size);
    }
  }

  stats_.small_memory_pieces = 0;
  stats_.huge_memory_pieces = 0;
  for (auto *free_mem = mem_list.flush(); free_mem;) {
    const auto next_mem = mem_list.get_next(free_mem);
    put_memory_back(free_mem, free_mem->size());
    free_mem = next_mem;
  }

  // update stat
  register_deallocation(0);
  ++stats_.defragmentation_calls;
}

void *unsynchronized_pool_resource::allocate_small_piece_from_fallback_resource(size_type aligned_size) noexcept {
  void *mem = fallback_resource_.get_from_pool(aligned_size, true);
  if (likely(mem != nullptr)) {
    memory_debug("allocate %d, pool was empty, allocated address from fallback resource %p\n", aligned_size, mem);
    return mem;
  }
  details::memory_chunk_tree::tree_node *smallest_huge_piece = huge_pieces_.extract_smallest();
  if (!smallest_huge_piece) {
    perform_defragmentation();
    if ((mem = try_allocate_small_piece(aligned_size))) {
      return mem;
    }
    smallest_huge_piece = huge_pieces_.extract_smallest();
  }
  if (smallest_huge_piece) {
    --stats_.huge_memory_pieces;
    if (const size_type fallback_resource_left_size = fallback_resource_.size()) {
      put_memory_back(fallback_resource_.memory_current(), fallback_resource_left_size);
    }
    fallback_resource_.init(smallest_huge_piece, details::memory_chunk_tree::get_chunk_size(smallest_huge_piece));
    memory_debug("fallback resource was empty, took piece from huge map\n");
  }
  return fallback_resource_.get_from_pool(aligned_size);
}

void *unsynchronized_pool_resource::perform_defragmentation_and_allocate_huge_piece(size_type aligned_size) noexcept {
  // тело этой фукнции специально унесено в cpp файл, что бы она не инлайнилась в метод allocate
  perform_defragmentation();
  return allocate_huge_piece(aligned_size, false);
}

} // namespace memory_resource

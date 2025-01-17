#include "memfd.hpp"

void MemoryFile::handleClose() {
	_cancelServe.cancel();
}

async::result<frg::expected<Error, off_t>>
MemoryFile::seek(off_t delta, VfsSeek whence) {
	if(whence == VfsSeek::absolute) {
		_offset = delta;
	}else if(whence == VfsSeek::relative){
		_offset += delta;
	}else if(whence == VfsSeek::eof) {
		assert(whence == VfsSeek::eof);
		_offset += delta;
	}
	co_return _offset;
}

async::result<frg::expected<protocols::fs::Error>> MemoryFile::truncate(size_t size) {
	co_return _resizeFile(size).map_error(protocols::fs::toFsProtoError);
}

async::result<frg::expected<protocols::fs::Error>>
MemoryFile::allocate(int64_t offset, size_t size) {
	assert(!offset);

	if(_seals & F_SEAL_WRITE)
		co_return protocols::fs::Error::insufficientPermissions;
	/* check if the file size is enough */
	if(offset + size <= _fileSize)
		co_return {};

	co_return _resizeFile(offset + size).map_error(protocols::fs::toFsProtoError);
}

FutureMaybe<helix::UniqueDescriptor>
MemoryFile::accessMemory() {
	co_return _memory.dup();
}

frg::expected<Error> MemoryFile::_resizeFile(size_t new_size) {
	if(new_size > _fileSize && _seals & F_SEAL_GROW)
		return Error::insufficientPermissions;

	if(new_size < _fileSize && _seals & F_SEAL_SHRINK)
		return Error::insufficientPermissions;

	_fileSize = new_size;

	size_t aligned_size = (new_size + 0xFFF) & ~size_t(0xFFF);
	if(aligned_size <= _areaSize)
		return {};

	if(_memory) {
		HEL_CHECK(helResizeMemory(_memory.getHandle(), aligned_size));
	}else{
		HelHandle handle;
		HEL_CHECK(helAllocateMemory(aligned_size, 0, nullptr, &handle));
		_memory = helix::UniqueDescriptor{handle};
	}

	_mapping = helix::Mapping{_memory, 0, aligned_size};
	_areaSize = aligned_size;

	return {};
}

async::result<frg::expected<protocols::fs::Error, int>>
MemoryFile::getSeals() {
	co_return int{_seals};
}

async::result<frg::expected<protocols::fs::Error, int>>
MemoryFile::addSeals(int seals) {
	if(_seals & F_SEAL_SEAL) {
		co_return protocols::fs::Error::insufficientPermissions;
	}

	_seals |= seals;
	co_return int{_seals};
}

async::result<frg::expected<Error, size_t>>
MemoryFile::writeAll(Process *, const void *data, size_t length) {
	if(_seals & F_SEAL_WRITE)
		co_return Error::insufficientPermissions;

	auto end_size = _offset + length;
	if(end_size > _fileSize) {
		FRG_CO_TRY(_resizeFile(end_size));
	}

	memcpy(reinterpret_cast<std::byte *>(_mapping.get()) + _offset, data, length);
	_offset += length;
	co_return length;
}

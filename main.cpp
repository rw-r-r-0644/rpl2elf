#include "elf.h"
#include "rpl2elf.h"

#include <excmd.h>
#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <zlib.h>

auto elfImportsRelocationAddress = 0x01000000;

uint32_t
getSectionIndex(const Rpl &rpl,
					 const Section &section)
{
	return static_cast<uint32_t>(&section - &rpl.sections[0]);
}


bool
readSection(std::ifstream &fh,
				Section &section,
				size_t i)
{
	// Read section header
	fh.read(reinterpret_cast<char*>(&section.header), sizeof(elf::SectionHeader));

	if (section.header.type == elf::SHT_NOBITS || !section.header.size) {
		return true;
	}

	// Read section data
	if (section.header.flags & elf::SHF_DEFLATED) {
		auto stream = z_stream {};
		auto ret = Z_OK;

		// Read the original size
		uint32_t size = 0;
		fh.seekg(section.header.offset.value());
		fh.read(reinterpret_cast<char *>(&size), sizeof(uint32_t));
		size = byte_swap(size);
		section.data.resize(size);

		// Inflate
		memset(&stream, 0, sizeof(stream));
		stream.zalloc = Z_NULL;
		stream.zfree = Z_NULL;
		stream.opaque = Z_NULL;

		ret = inflateInit(&stream);

		if (ret != Z_OK) {
			fmt::print("Couldn't decompress .rpx section because inflateInit returned {}\n", ret);
			section.data.clear();
			return false;
		} else {
			std::vector<char> temp;
			temp.resize(section.header.size-sizeof(uint32_t));
			fh.read(temp.data(), temp.size());

			stream.avail_in = section.header.size;
			stream.next_in = reinterpret_cast<Bytef *>(temp.data());
			stream.avail_out = static_cast<uInt>(section.data.size());
			stream.next_out = reinterpret_cast<Bytef *>(section.data.data());

			ret = inflate(&stream, Z_FINISH);

			if (ret != Z_OK && ret != Z_STREAM_END) {
				fmt::print("Couldn't decompress .rpx section because inflate returned {}\n", ret);
				section.data.clear();
				return false;
			}

			inflateEnd(&stream);
		}
	} else {
		section.data.resize(section.header.size);
		fh.seekg(section.header.offset.value());
		fh.read(section.data.data(), section.header.size);
	}

	return true;
}

/**
 * Read the .rpl file
 */
static bool
readRpl(Rpl &rpl, const std::string &path)
{
	// Read file
	std::ifstream fh { path, std::ifstream::binary };
	if (!fh.is_open()) {
		fmt::print("Could not open {} for reading\n", path);
		return false;
	}

	fh.read(reinterpret_cast<char*>(&rpl.header), sizeof(elf::Header));

	if (rpl.header.magic != elf::HeaderMagic) {
		fmt::print("Invalid ELF magic header\n");
		return false;
	}
	
	// Read sections
	for (auto i = 0u; i < rpl.header.shnum; ++i) {
		Section section;
		fh.seekg(rpl.header.shoff + rpl.header.shentsize * i);

		if (!readSection(fh, section, i)) {
			fmt::print("Error reading section {}", i);
			return false;
		}

		rpl.sections.push_back(section);
	}
	
	// Set section names
	auto shStrTab = reinterpret_cast<const char *>(rpl.sections[rpl.header.shstrndx].data.data());
	for (auto &section : rpl.sections) {
		section.name = shStrTab + section.header.name;
	}
	
	return true;
}

/**
 * Fix file header to look like an ELF file!
 */
static bool
fixFileHeader(Rpl &file)
{
	file.header.abi = elf::EABI_NONE;
	file.header.type = uint16_t { elf::ET_EXEC };
	return true;
}

/**
 * Fix relocations.
 * Replace non-standard GHS_REL16 relocations
 */
static bool
fixRelocations(Rpl &file)
{
	for (auto &section : file.sections) {
		std::vector<elf::Rela> newRelocations;

		if (section.header.type != elf::SHT_RELA) {
			continue;
		}

		// Clear flags
		section.header.flags = 0u;

		auto &symbolSection = file.sections[section.header.link];
		auto &targetSection = file.sections[section.header.info];

		auto rels = reinterpret_cast<elf::Rela *>(section.data.data());
		auto numRels = section.data.size() / sizeof(elf::Rela);
		for (auto i = 0u; i < numRels; ++i) {
			auto info = rels[i].info;
			auto addend = rels[i].addend;
			auto offset = rels[i].offset;
			auto index = info >> 8;
			auto type = info & 0xFF;
			
			if (!info && !addend && !offset)
				continue;

			switch (type) {
			case elf::R_PPC_NONE:
			case elf::R_PPC_ADDR32:
			case elf::R_PPC_ADDR16_LO:
			case elf::R_PPC_ADDR16_HI:
			case elf::R_PPC_ADDR16_HA:
			case elf::R_PPC_REL24:
			case elf::R_PPC_REL14:
			case elf::R_PPC_DTPMOD32:
			case elf::R_PPC_DTPREL32:
			case elf::R_PPC_EMB_SDA21:
			case elf::R_PPC_EMB_RELSDA:
			case elf::R_PPC_DIAB_SDA21_LO:
			case elf::R_PPC_DIAB_SDA21_HI:
			case elf::R_PPC_DIAB_SDA21_HA:
			case elf::R_PPC_DIAB_RELSDA_LO:
			case elf::R_PPC_DIAB_RELSDA_HI:
			case elf::R_PPC_DIAB_RELSDA_HA:
			{
				// All valid relocations
				newRelocations.emplace_back();
				auto &newRel = newRelocations.back();
				newRel.info = info;
				newRel.addend = addend;
				newRel.offset = offset;
				break;
			}
			
			/*
			 * Convert two GHS_REL16 into a R_PPC_REL32
			 */
			case elf::R_PPC_GHS_REL16_HI:
			{
				bool success = false;
				
				// Attempt to find an R_PPC_GHS_REL16_LO to make a R_PPC_REL32
				for (auto j = 0u; j < numRels; ++j) {
					if (rels[j].info != ((index << 8) | elf::R_PPC_GHS_REL16_LO)) continue;
					if (rels[j].addend != (addend + 2)) continue;
					if (rels[j].offset != (offset + 2)) continue;
					
					newRelocations.emplace_back();
					auto &newRel = newRelocations.back();

					newRel.info = (index << 8) | elf::R_PPC_REL32;
					newRel.addend = addend;
					newRel.offset = offset;
					
					rels[j].info = 0u;
					rels[j].addend = 0;
					rels[j].offset = 0u;
					
					//fmt::print("Successfully converted GHS_REL16 group to R_PPC_REL32!\n");
					success = true;
				}
				
				if (!success)
					fmt::print("Unsupported relocation found! Unable to fix\n");

				break;
			}
			
			case elf::R_PPC_GHS_REL16_LO:
			{
				bool success = false;
				
				// Attempt to find an R_PPC_GHS_REL16_HI to make a R_PPC_REL32
				for (auto j = 0u; j < numRels; ++j) {
					if (rels[j].info != ((index << 8) | elf::R_PPC_GHS_REL16_HI)) continue;
					if (rels[j].addend != (addend - 2)) continue;
					if (rels[j].offset != (offset - 2)) continue;
					
					newRelocations.emplace_back();
					auto &newRel = newRelocations.back();

					newRel.info = (index << 8) | elf::R_PPC_REL32;
					newRel.addend = addend - 2;
					newRel.offset = offset - 2;
					
					rels[j].info = 0u;
					rels[j].addend = 0;
					rels[j].offset = 0u;
					
					fmt::print("Successfully converted GHS_REL16 group to R_PPC_REL32!\n");
					success = true;
				}
				
				if (!success)
					fmt::print("Unsupported relocation found! Unable to fix\n");

				break;
			}

			default:
				fmt::print("Unknown relocation found!\n");
				break;
			}
		}

		section.data.clear();
		section.data.insert(section.data.end(),
									reinterpret_cast<char *>(newRelocations.data()),
									reinterpret_cast<char *>(newRelocations.data() + newRelocations.size()));
	}

	return true;
}

/**
 * Calculate section file offsets.
 */
static bool
calculateSectionOffsets(Rpl &file)
{
	auto offset = file.header.shoff;
	offset += align_up(static_cast<uint32_t>(file.sections.size() * sizeof(elf::SectionHeader)), 64);

	for (auto &section : file.sections) {
		if (section.header.type == elf::SHT_NOBITS ||
			section.header.type == elf::SHT_NULL) {
			section.header.offset = 0u;
			section.data.clear();
		}
	}

	for (auto &section : file.sections) {
		if (section.header.type == elf::SHT_RPL_CRCS) {
			section.header.offset = offset;
			section.header.size = static_cast<uint32_t>(section.data.size());
			offset += section.header.size;
		}
	}

	for (auto &section : file.sections) {
		if (section.header.type == elf::SHT_RPL_FILEINFO) {
			section.header.offset = offset;
			section.header.size = static_cast<uint32_t>(section.data.size());
			offset += section.header.size;
		}
	}

	// First the "dataMin / dataMax" sections, which are:
	// - !(flags & SHF_EXECINSTR)
	// - flags & SHF_WRITE
	// - flags & SHF_ALLOC
	for (auto &section : file.sections) {
		if (section.header.size == 0 ||
			section.header.type == elf::SHT_RPL_FILEINFO ||
			section.header.type == elf::SHT_RPL_IMPORTS ||
			section.header.type == elf::SHT_RPL_CRCS ||
			section.header.type == elf::SHT_NOBITS) {
			continue;
		}

		if (!(section.header.flags & elf::SHF_EXECINSTR) &&
			  (section.header.flags & elf::SHF_WRITE) &&
			  (section.header.flags & elf::SHF_ALLOC)) {
			section.header.offset = offset;
			section.header.size = static_cast<uint32_t>(section.data.size());
			offset += section.header.size;
		}
	}

	// Next the "readMin / readMax" sections, which are:
	// - !(flags & SHF_EXECINSTR) || type == SHT_RPL_EXPORTS
	// - !(flags & SHF_WRITE)
	// - flags & SHF_ALLOC
	for (auto &section : file.sections) {
		if (section.header.size == 0 ||
			 section.header.type == elf::SHT_RPL_FILEINFO ||
			 section.header.type == elf::SHT_RPL_IMPORTS ||
			 section.header.type == elf::SHT_RPL_CRCS ||
			 section.header.type == elf::SHT_NOBITS) {
			continue;
		}

		if ((!(section.header.flags & elf::SHF_EXECINSTR) ||
				 section.header.type == elf::SHT_RPL_EXPORTS) &&
			 !(section.header.flags & elf::SHF_WRITE) &&
			  (section.header.flags & elf::SHF_ALLOC)) {
			section.header.offset = offset;
			section.header.size = static_cast<uint32_t>(section.data.size());
			offset += section.header.size;
		}
	}

	// Import sections are part of the read sections, but have execinstr flag set
	// so let's insert them here to avoid complicating the above logic.
	for (auto &section : file.sections) {
		if (section.header.type == elf::SHT_RPL_IMPORTS) {
			section.header.offset = offset;
			section.header.size = static_cast<uint32_t>(section.data.size());
			offset += section.header.size;
		}
	}

	// Next the "textMin / textMax" sections, which are:
	// - flags & SHF_EXECINSTR
	// - type != SHT_RPL_EXPORTS
	for (auto &section : file.sections) {
		if (section.header.size == 0 ||
			 section.header.type == elf::SHT_RPL_FILEINFO ||
			 section.header.type == elf::SHT_RPL_IMPORTS ||
			 section.header.type == elf::SHT_RPL_CRCS ||
			 section.header.type == elf::SHT_NOBITS) {
			continue;
		}

		if ((section.header.flags & elf::SHF_EXECINSTR) &&
			  section.header.type != elf::SHT_RPL_EXPORTS) {
			section.header.offset = offset;
			section.header.size = static_cast<uint32_t>(section.data.size());
			offset += section.header.size;
		}
	}

	// Next the "tempMin / tempMax" sections, which are:
	// - !(flags & SHF_EXECINSTR)
	// - !(flags & SHF_ALLOC)
	for (auto &section : file.sections) {
		if (section.header.size == 0 ||
			 section.header.type == elf::SHT_RPL_FILEINFO ||
			 section.header.type == elf::SHT_RPL_IMPORTS ||
			 section.header.type == elf::SHT_RPL_CRCS ||
			 section.header.type == elf::SHT_NOBITS) {
			continue;
		}

		if (!(section.header.flags & elf::SHF_EXECINSTR) &&
			 !(section.header.flags & elf::SHF_ALLOC)) {
			section.header.offset = offset;
			section.header.size = static_cast<uint32_t>(section.data.size());
			offset += section.header.size;
		}
	}

	auto index = 0u;
	for (auto &section : file.sections) {
		if (section.header.offset == 0 &&
			 section.header.type != elf::SHT_NULL &&
			 section.header.type != elf::SHT_NOBITS) {
			fmt::print("Failed to calculate offset for section {}\n", index);
			return false;
		}

		++index;
	}

	return true;
}

/**
 * Write out the final ELF.
 */
static bool
writeElf(Rpl &file, const std::string &filename)
{
	auto shoff = file.header.shoff;

	// Write the file out
	std::ofstream out { filename, std::ofstream::binary };

	if (!out.is_open()) {
		fmt::print("Could not open {} for writing\n", filename);
		return false;
	}

	// Write file header
	out.seekp(0, std::ios::beg);
	out.write(reinterpret_cast<const char *>(&file.header), sizeof(elf::Header));

	// Write section headers
	out.seekp(shoff, std::ios::beg);
	for (const auto &section : file.sections) {
		out.write(reinterpret_cast<const char *>(&section.header), sizeof(elf::SectionHeader));
	}

	// Write sections
	for (const auto &section : file.sections) {
		if (section.data.size()) {
			out.seekp(section.header.offset, std::ios::beg);
			out.write(section.data.data(), section.data.size());
		}
	}

	return true;
}

/**
 * Relocate a section to a new address.
 */
static bool
relocateSection(Rpl &file,
					 Section &section,
					 uint32_t sectionIndex,
					 uint32_t newSectionAddress)
{
	auto sectionSize = section.data.size() ? section.data.size() : static_cast<size_t>(section.header.size);
	auto oldSectionAddress = section.header.addr;
	auto oldSectionAddressEnd = section.header.addr + sectionSize;

	// Relocate symbols pointing into this section
	for (auto &symSection : file.sections) {
		if (symSection.header.type != elf::SectionType::SHT_SYMTAB) {
			continue;
		}

		auto symbols = reinterpret_cast<elf::Symbol *>(symSection.data.data());
		auto numSymbols = symSection.data.size() / sizeof(elf::Symbol);
		for (auto i = 0u; i < numSymbols; ++i) {
			auto type = symbols[i].info & 0xf;
			auto value = symbols[i].value;

			// Only relocate data, func, section symbols
			if (type != elf::STT_OBJECT &&
				 type != elf::STT_FUNC &&
				 type != elf::STT_SECTION) {
				continue;
			}

			if (value >= oldSectionAddress && value <= oldSectionAddressEnd) {
				symbols[i].value = (value - oldSectionAddress) + newSectionAddress;
			}
		}
	}

	// Relocate relocations pointing into this section
	for (auto &relaSection : file.sections) {
		if (relaSection.header.type != elf::SectionType::SHT_RELA ||
			 relaSection.header.info != sectionIndex) {
			continue;
		}

		auto rela = reinterpret_cast<elf::Rela *>(relaSection.data.data());
		auto numRelas = relaSection.data.size() / sizeof(elf::Rela);
		for (auto i = 0u; i < numRelas; ++i) {
			auto offset = rela[i].offset;

			if (offset >= oldSectionAddress && offset <= oldSectionAddressEnd) {
				rela[i].offset = (offset - oldSectionAddress) + newSectionAddress;
			}
		}
	}

	section.header.addr = newSectionAddress;
	return true;
}

bool relocateImports(Rpl &file)
{
	auto newLoc = elfImportsRelocationAddress;

	// Relocate .symtab and .strtab to be in loader memory
	for (auto i = 0u; i < file.sections.size(); ++i) {
		auto &section = file.sections[i];
		if (section.header.type == elf::SHT_RPL_IMPORTS) {
			relocateSection(file, section, i, align_up(newLoc, section.header.addralign));
			section.header.flags |= elf::SHF_ALLOC;
			newLoc += section.data.size();
		}
	}

	return true;
}

int main(int argc, char **argv)
{
	excmd::parser parser;
	excmd::option_state options;
	using excmd::description;
	using excmd::value;

	try {
		parser.global_options()
			.add_option("H,help",
							description { "Show help." });

		parser.default_command()
			.add_argument("src",
							  description { "Path to input elf file" },
							  value<std::string> {})
			.add_argument("dst",
							  description { "Path to output rpl file" },
							  value<std::string> {});

		options = parser.parse(argc, argv);
	} catch (excmd::exception ex) {
		fmt::print("Error parsing options: {}\n", ex.what());
		return -1;
	}

	if (options.empty()
		 || options.has("help")
		 || !options.has("src")
		 || !options.has("dst")) {
		fmt::print("{} <options> src dst\n", argv[0]);
		fmt::print("{}\n", parser.format_help(argv[0]));
		return 0;
	}

	auto src = options.get<std::string>("src");
	auto dst = options.get<std::string>("dst");

	Rpl rpl;

	if (!readRpl(rpl, src)) {
		fmt::print("ERROR: readRpl failed.\n");
		return -1;
	}
	
	if (!fixFileHeader(rpl)) {
		fmt::print("ERROR: fixFileHeader failed.\n");
		return -1;
	}

	if (!fixRelocations(rpl)) {
		fmt::print("ERROR: fixRelocations failed.\n");
		return -1;
	}
	
	if (!relocateImports(rpl)) {
		fmt::print("ERROR: relocateImports failed.\n");
		return -1;
	}
	
	if (!calculateSectionOffsets(rpl)) {
		fmt::print("ERROR: calculateSectionOffsets failed.\n");
		return -1;
	}
	
	if (!writeElf(rpl, dst)) {
		fmt::print("ERROR: writeElf failed.\n");
		return -1;
	}
	
	return 0;
}

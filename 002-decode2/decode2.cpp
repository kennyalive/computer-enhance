#include <cassert>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

std::vector<uint8_t> read_binary_file(const std::string& file_name)
{
    FILE* f = fopen(file_name.c_str(), "rb");
    if (!f) {
        return {};
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> bytes(file_size);
    if (fread(bytes.data(), 1, file_size, f) != file_size) {
        fclose(f);
        return {};
    }
    fclose(f);
    return bytes;
}

struct Byte_Fetcher
{
    bool has_bytes() const
    {
        return current_byte < bytes.size();
    }
    uint8_t get_current_byte() const
    {
        return bytes[current_byte];
    }
    uint8_t fetch()
    {
        assert(current_byte != bytes.size());
        return bytes[current_byte++];
    }
    void fetch(uint8_t* buffer, int byte_count)
    {
        assert(current_byte + byte_count <= bytes.size());
        for (int i = 0; i < byte_count; i++) {
            buffer[i] = bytes[current_byte + i];
        }
        current_byte += byte_count;
    }
    std::vector<uint8_t> bytes;
    size_t current_byte = 0;
};

const char* decode_register(uint8_t reg, bool w)
{
    assert(reg < 8);
    static const char* register_names[8][2] = {
        {"al", "ax"},
        {"cl", "cx"},
        {"dl", "dx"},
        {"bl", "bx"},
        {"ah", "sp"},
        {"ch", "bp"},
        {"dh", "si"},
        {"bh", "di"}
    };
    return register_names[reg][w];
}

bool decode_mov(Byte_Fetcher& fetcher)
{
    uint8_t bytes[2];
    fetcher.fetch(bytes, 2);

    const uint8_t MOD = bytes[1] >> 6;
    const uint8_t REG = (bytes[1] >> 3) & 7;
    const uint8_t RM = bytes[1] & 7;
    const bool D = (bytes[0] >> 1) & 1;
    const bool W = bytes[0] & 1;
    if (MOD != 3) {
        return false;;
    }
    const uint8_t dst = D ? REG : RM;
    const char* dst_reg_name = decode_register(dst, W);
    const uint8_t src = D ? RM : REG;
    const char* src_reg_name = decode_register(src, W);
    printf("mov %s, %s\n", dst_reg_name, src_reg_name);
    return true;
}

bool decode_immediate_mov(Byte_Fetcher& fetcher)
{
    const uint8_t byte0 = fetcher.fetch();

    const bool W = (byte0 >> 3) & 1;

    const uint8_t REG = byte0 & 7;
    const char* reg_name = decode_register(REG, W);

    if (W) {
        uint8_t data[2];
        fetcher.fetch(data, 2);
        printf("mov %s, %u\n", reg_name, (data[0] | data[1] << 8));
    }
    else {
        const uint8_t data = fetcher.fetch();
        printf("mov %s, %u\n", reg_name, data);
    }
    return true;
}

bool decode(Byte_Fetcher& byte_fetcher)
{
    struct Opcode_Info {
        uint8_t opcode;
        uint8_t opcode_bit_count;
    };
    const Opcode_Info move_opcode = { 0b100010, 6 };
    const Opcode_Info immediate_move_opcode = { 0b1011, 4 };

    auto test_opcode = [](uint8_t byte0, const Opcode_Info& opcode_info) {
        assert(opcode_info.opcode_bit_count <= 8);
        return (byte0 >> (8 - opcode_info.opcode_bit_count)) == opcode_info.opcode;
    };

    const uint8_t byte0 = byte_fetcher.get_current_byte();

    if (test_opcode(byte0, move_opcode)) {
        return decode_mov(byte_fetcher);
    }
    else if (test_opcode(byte0, immediate_move_opcode)) {
        return decode_immediate_mov(byte_fetcher);
    }
    return false;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: decode <binary_asm_file>\n");
        return 0;
    }
    printf("bits 16\n");
    Byte_Fetcher byte_fetcher;
    byte_fetcher.bytes = read_binary_file(argv[1]);
    while (byte_fetcher.has_bytes()) {
        if (!decode(byte_fetcher)) {
            // Stop decoding if find something we don't support
            break; 
        }
    }
    return 0;
}

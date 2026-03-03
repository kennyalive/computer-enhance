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

void decode(uint8_t byte0, uint8_t byte1)
{
    const uint8_t move_opcode = 0b100010;

    const uint8_t OPCODE = byte0 >> 2;
    const uint8_t MOD = byte1 >> 6;
    const uint8_t REG = (byte1 >> 3) & 7;
    const uint8_t RM = byte1 & 7;
    const bool D = (byte0 >> 1) & 1;
    const bool W = byte0 & 1;

    if (OPCODE != move_opcode) {
        return;
    }
    if (MOD != 3) {
        return;
    }

    const uint8_t dst = D ? REG : RM;
    const char* dst_reg_name = decode_register(dst, W);

    const uint8_t src = D ? RM : REG;
    const char* src_reg_name = decode_register(src, W);

    printf("mov %s, %s\n", dst_reg_name, src_reg_name);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: decode <binary_asm_file>\n");
        return 0;
    }
    printf("bits 16\n");
    std::vector<uint8_t> bytes = read_binary_file(argv[1]);
    for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
        decode(bytes[i], bytes[i + 1]);
    }
    return 0;
}

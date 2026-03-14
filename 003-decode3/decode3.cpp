#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

struct String
{
    char data[32] = {};
    int length = 0;
    String(const char* str)
    {
        append(str);
    }
    String(uint32_t x)
    {
        append(x);
    }
    void append(const char* str, int str_len)
    {
        assert(strlen(str) == str_len);
        length += str_len;
        assert(length < 32);
        strcat(data, str);
    }
    void append(const char* str)
    {
        int n = (int)strlen(str);
        append(str, n);
    }
    void append(uint32_t x)
    {
        char s[16];
        int n = snprintf(s, 16, "%u", x);
        append(s, n);
    }
};

struct Bytes
{
    uint8_t* data = nullptr;
    size_t count = 0;

    Bytes() = default;
    Bytes(size_t size)
        : data(new uint8_t[size])
        , count(size)
    {}
    Bytes(Bytes&& other) noexcept
    {
        *this = static_cast<Bytes&&>(other);
    }
    ~Bytes()
    {
        delete[] data;
    }
    Bytes& operator=(Bytes&& other) noexcept
    {
        delete[] data;
        data = other.data;
        count = other.count;
        other.data = nullptr;
        other.count = 0;
        return *this;
    }
    uint8_t operator[](size_t index) const
    {
        assert(index < count);
        return data[index];
    }
};

struct Fetcher
{
    bool has_bytes() const
    {
        return current_byte < bytes.count;
    }
    uint8_t get_current_byte() const
    {
        assert(has_bytes());
        return bytes[current_byte];
    }
    uint8_t fetch_byte()
    {
        assert(current_byte != bytes.count);
        return bytes[current_byte++];
    }
    uint16_t fetch_word()
    {
        assert(current_byte + 2 <= bytes.count);
        uint16_t word = bytes[current_byte] | (bytes[current_byte + 1] << 8);
        current_byte += 2;
        return word;
    }
    int32_t fetch_byte_or_word_imm(bool W)
    {
        return W ? fetch_word() : static_cast<int8_t>(fetch_byte());
    }
    Bytes bytes;
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

const char* get_effective_address_formula(uint8_t RM)
{
    assert(RM < 8);
    static const char* effective_address_str[8] = {
        "bx + si",
        "bx + di",
        "bp + si",
        "bp + di",
        "si",
        "di",
        "bp",
        "bx"
    };
    return effective_address_str[RM];
}

const char* decode_imm_to_rm_opcode_name(uint8_t byte1)
{
    static const char* opcode_names[8] = {
        "add",
        "or",
        "adc",
        "sbb",
        "and",
        "sub",
        nullptr,
        "cmp"
    };
    uint8_t k = (byte1 >> 3) & 7;
    return opcode_names[k];
}

String decode_effective_address(uint8_t MOD, uint8_t RM, Fetcher& fetcher)
{
    assert(MOD < 3);
    assert(RM < 8);

    if (MOD == 0 && RM == 0b110) { // direct access
        const uint16_t displacement = fetcher.fetch_word();
        return String(displacement);
    }
    int32_t displacement = 0;
    if (MOD == 1) {
        // single byte displacement is signed value: [-128..127]
        displacement = static_cast<int8_t>(fetcher.fetch_byte());
    }
    else if (MOD == 2) {
        displacement = fetcher.fetch_word();
    }
    String ea_str(get_effective_address_formula(RM));
    if (displacement < 0) {
        ea_str.append(" - ");
        ea_str.append(-displacement);
    }
    else if (displacement > 0) {
        ea_str.append(" + ");
        ea_str.append(displacement);
    }
    return ea_str;
}

bool decode_reg_mr(Fetcher& fetcher, const char* opcode_name)
{
    const uint8_t byte0 = fetcher.fetch_byte();
    const uint8_t byte1 = fetcher.fetch_byte();
    const bool D = (byte0 >> 1) & 1;
    const bool W = byte0 & 1;
    const uint8_t MOD = byte1 >> 6;
    const uint8_t REG = (byte1 >> 3) & 7;
    const uint8_t RM = byte1 & 7;

    if (MOD == 3) { // mov reg, reg
        const uint8_t dst = D ? REG : RM;
        const char* dst_reg_name = decode_register(dst, W);
        const uint8_t src = D ? RM : REG;
        const char* src_reg_name = decode_register(src, W);
        printf("%s %s, %s\n", opcode_name, dst_reg_name, src_reg_name);
    }
    else {
        const char* reg_name = decode_register(REG, W);
        String effective_address = decode_effective_address(MOD, RM, fetcher);
        if (D) {
            printf("%s %s, [%s]\n", opcode_name, reg_name, effective_address.data);
        }
        else {
            printf("%s [%s], %s\n", opcode_name, effective_address.data, reg_name);
        }
    }
    return true;
}

bool decode_imm_to_rm(Fetcher& fetcher, bool has_S, const char* opcode_name)
{
    const uint8_t byte0 = fetcher.fetch_byte();
    const uint8_t byte1 = fetcher.fetch_byte();
    const bool W = byte0 & 1;
    const bool S = has_S && (byte0 & 2);
    const uint8_t MOD = byte1 >> 6;
    const uint8_t RM = byte1 & 7;

    if (opcode_name == nullptr) {
        opcode_name = decode_imm_to_rm_opcode_name(byte1);
    }

    if (MOD == 3) {
        const char* reg_name = decode_register(RM, W);
        int32_t imm;
        if (W) {
            imm = S ? static_cast<int8_t>(fetcher.fetch_byte()) : fetcher.fetch_word();
        }
        else {
            imm = fetcher.fetch_byte();

        }
        printf("%s %s, %d\n", opcode_name, reg_name, imm);
    }
    else {
        String effective_address = decode_effective_address(MOD, RM, fetcher);
        if (W) {
            const uint16_t imm = S ? static_cast<int8_t>(fetcher.fetch_byte()) : fetcher.fetch_word();
            printf("%s [%s], word %u\n", opcode_name, effective_address.data, imm);
        }
        else {
            const uint8_t imm = fetcher.fetch_byte();
            printf("%s byte [%s], %u\n", opcode_name, effective_address.data, imm);
        }
    }
    return true;
}

bool decode_imm_to_rm(Fetcher& fetcher)
{
    return decode_imm_to_rm(fetcher, true, nullptr);
}

bool decode_move_reg_mr(Fetcher& fetcher)
{
    return decode_reg_mr(fetcher, "mov");
}

bool decode_move_imm_to_reg(Fetcher& fetcher)
{
    const uint8_t byte0 = fetcher.fetch_byte();
    const bool W = (byte0 >> 3) & 1;
    const uint8_t REG = byte0 & 7;
    const char* reg_name = decode_register(REG, W);
    const uint32_t imm = W ? fetcher.fetch_word() : fetcher.fetch_byte();
    printf("mov %s, %u\n", reg_name, imm);
    return true;
}

bool decode_move_imm_to_rm(Fetcher& fetcher)
{
    return decode_imm_to_rm(fetcher, false, "mov");
}

bool decode_move_mem_to_accum(Fetcher& fetcher)
{
    const uint8_t byte0 = fetcher.fetch_byte();
    const uint16_t address = fetcher.fetch_word();
    const bool W = byte0 & 1;
    printf("mov %s, [%u]\n", W ? "ax" : "al", address);
    return true;
}

bool decode_move_accum_to_mem(Fetcher& fetcher)
{
    const uint8_t byte0 = fetcher.fetch_byte();
    const uint16_t address = fetcher.fetch_word();
    const bool W = byte0 & 1;
    printf("mov [%u], %s\n", address, W ? "ax" : "al");
    return true;
}

bool decode_add_reg_mr(Fetcher& fetcher)
{
    return decode_reg_mr(fetcher, "add");
}

bool decode_add_imm_to_rm(Fetcher& fetcher)
{
    return decode_imm_to_rm(fetcher, true, "add");
}

bool decode_imm_accum(Fetcher& fetcher, const char* opcode_name)
{
    const uint8_t byte0 = fetcher.fetch_byte();
    const bool W = byte0 & 1;
    const int32_t imm = fetcher.fetch_byte_or_word_imm(W);
    printf("%s %s, %d\n", opcode_name, W ? "ax" : "al", imm);
    return true;
}

bool decode_add_imm_to_accum(Fetcher& fetcher)
{
    return decode_imm_accum(fetcher, "add");
}

bool decode_sub_reg_mr(Fetcher& fetcher)
{
    return decode_reg_mr(fetcher, "sub");
}

bool decode_sub_imm_from_accum(Fetcher& fetcher)
{
    return decode_imm_accum(fetcher, "sub");
}

bool decode(Fetcher& fetcher)
{
    using Decoder = bool(*)(Fetcher&);
    struct Opcode_Info {
        uint8_t opcode;
        uint8_t opcode_mask;
        Decoder decoder;
    };
    static const Opcode_Info opcode_infos[] = {
        {0b100000'00, uint8_t(~0x3), decode_imm_to_rm},
        {0b100010'00, uint8_t(~0x3), decode_move_reg_mr},
        {0b1011'0000, uint8_t(~0xf), decode_move_imm_to_reg},
        {0b1100011'0, uint8_t(~0x1), decode_move_imm_to_rm},
        {0b1010000'0, uint8_t(~0x1), decode_move_mem_to_accum},
        {0b1010001'0, uint8_t(~0x1), decode_move_accum_to_mem},
        {0b000000'00, uint8_t(~0x3), decode_add_reg_mr},
        {0b0000010'0, uint8_t(~0x1), decode_add_imm_to_accum},
        {0b001010'00, uint8_t(~0x3), decode_sub_reg_mr},
        {0b0010110'0, uint8_t(~0x1), decode_sub_imm_from_accum},
    };
    const uint8_t byte0 = fetcher.get_current_byte();
    Decoder decoder = nullptr;
    for (const Opcode_Info& info : opcode_infos) {
        if ((byte0 & info.opcode_mask) == info.opcode) {
            decoder = info.decoder;
            break;
        }
    }
    if (!decoder) {
        return false;
    }
    return decoder(fetcher);
}

Bytes read_binary_file(const char* file_name)
{
    FILE* f = fopen(file_name, "rb");
    if (!f) {
        return {};
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    Bytes bytes(file_size);
    if (fread(bytes.data, 1, file_size, f) != file_size) {
        fclose(f);
        return {};
    }
    fclose(f);
    return bytes;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: decode <binary_asm_file>\n");
        return 0;
    }
    printf("bits 16\n");
    Fetcher fetcher;
    fetcher.bytes = read_binary_file(argv[1]);
    while (fetcher.has_bytes()) {
        if (!decode(fetcher)) {
            // Stop decoding if find something we don't support
            break; 
        }
    }
    return 0;
}

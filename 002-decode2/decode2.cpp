#include <cassert>
#include <cstdint>
#include <cstdio>

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
        return bytes[current_byte];
    }
    uint8_t fetch()
    {
        assert(current_byte != bytes.count);
        return bytes[current_byte++];
    }
    void fetch(uint8_t* buffer, int byte_count)
    {
        assert(current_byte + byte_count <= bytes.count);
        for (int i = 0; i < byte_count; i++) {
            buffer[i] = bytes[current_byte + i];
        }
        current_byte += byte_count;
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

bool decode_mov(Fetcher& fetcher)
{
    uint8_t bytes[2];
    fetcher.fetch(bytes, 2);

    const uint8_t MOD = bytes[1] >> 6;
    const uint8_t REG = (bytes[1] >> 3) & 7;
    const uint8_t RM = bytes[1] & 7;
    const bool D = (bytes[0] >> 1) & 1;
    const bool W = bytes[0] & 1;

    if (MOD == 3) { // mov reg, reg
        const uint8_t dst = D ? REG : RM;
        const char* dst_reg_name = decode_register(dst, W);
        const uint8_t src = D ? RM : REG;
        const char* src_reg_name = decode_register(src, W);
        printf("mov %s, %s\n", dst_reg_name, src_reg_name);
    }
    else {
        bool direct_address = false;
        bool has_displacement = false;
        uint16_t displacement = 0;
        if (MOD == 0) {
            if (RM == 0b110) {
                displacement = fetcher.fetch() | (fetcher.fetch() << 8);
                direct_address = true;
                has_displacement = true;
            }
        }
        else if (MOD == 1) {
            displacement = fetcher.fetch();
            has_displacement = true;
        }
        else {
            assert(MOD == 2);
            displacement = fetcher.fetch() | (fetcher.fetch() << 8);
            has_displacement = true;
        }
        if (direct_address) {
            return false;
        }
        const char* reg_name = decode_register(REG, W);
        const char* effective_address_formula = get_effective_address_formula(RM);
        char displacement_str[16];
        snprintf(displacement_str, 16, "%u", displacement);
        if (D) {
            printf("mov %s, [%s%s%s]\n",
                reg_name,
                effective_address_formula,
                has_displacement ? " + " : "",
                has_displacement ? displacement_str : ""
            );
        }
        else {
            printf("mov [%s%s%s], %s\n",
                effective_address_formula,
                has_displacement ? " + " : "",
                has_displacement ? displacement_str : "",
                reg_name
            );
        }
    }
    return true;
}

bool decode_immediate_mov(Fetcher& fetcher)
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

bool decode(Fetcher& fetcher)
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

    const uint8_t byte0 = fetcher.get_current_byte();

    if (test_opcode(byte0, move_opcode)) {
        return decode_mov(fetcher);
    }
    else if (test_opcode(byte0, immediate_move_opcode)) {
        return decode_immediate_mov(fetcher);
    }
    return false;
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

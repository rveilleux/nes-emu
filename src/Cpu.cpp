#include "Cpu.h"
#include "Nes.h"
#include "Memory.h"
#include "OpCodeTable.h"
#include "System.h"

#define ADDR_8 "$%02X"
#define ADDR_16 "$%04X"

// If set, debugging features are enabled for the emulator (slower)
#define DEBUGGING_ENABLED 1

namespace
{
	OpCodeEntry** g_ppOpCodeTable = GetOpCodeTable();

	FORCEINLINE uint8 CalcNegativeFlag(uint16 v)
	{
		// Check if bit 7 is set
		return (v & 0x0080) != 0;
	}

	FORCEINLINE uint8 CalcNegativeFlag(uint8 v)
	{
		// Check if bit 7 is set
		return (v & 0x80) != 0;
	}

	FORCEINLINE uint8 CalcZeroFlag(uint16 v)
	{
		// Check that lower 8 bits are all 0
		return (v & 0x00FF) == 0;
	}

	FORCEINLINE uint8 CalcZeroFlag(uint8 v)
	{
		return v == 0;
	}

	FORCEINLINE uint8 CalcCarryFlag(uint16 v)
	{
		// Check if upper 8 bits are non-zero to know if a carry occured
		return (v & 0xFF00) != 0;
	}

	// Force link time error: need 16 bit to compute carry
	FORCEINLINE uint8 CalcCarryFlag(uint8 v);

	FORCEINLINE uint8 CalcOverflowFlag(uint8 a, uint8 b, uint16 r)
	{
		// With r = a + b, overflow occurs if both a and b are negative and r is positive,
		// or both a and b are positive and r is negative. Looking at sign bits of a, b, r,
		// overflow occurs when 0 0 1 or 1 1 0, so we can use simple xor logic to figure it out.
		return ((uint16)a ^ r) & ((uint16)b ^ r) & 0x0080;
	}

	// Force link time error: need 16 bit result to compute overflow
	FORCEINLINE uint8 CalcOverflowFlag(uint8 a, uint8 b, uint8 r);
}

namespace StatusFlag
{
	enum Type : uint8
	{
		Carry			= 0x01,
		Zero			= 0x02,
		InterruptsOff	= 0x04, // Interrupt (IRQ) disabled
		Decimal			= 0x08,
		BrkExecuted		= 0x10, // BRK executed (IRQ/software interupt)
		Unused			= 0x20,
		Overflow		= 0x40, // 'V'
		Negative		= 0x80, // aka Sign flag
	};		
}

void Cpu::Initialize(Nes& nes, CpuRam& cpuRam)
{
	m_quit = false;
	m_pNes = &nes;
	m_pRam = &cpuRam;
}

void Cpu::Reset()
{
	// See http://wiki.nesdev.com/w/index.php/CPU_power_up_state

	A = X = Y = 0;
	SP = 0x0FD;
	
	P.ClearAll();
	P.Set(StatusFlag::Unused);
	P.Set(StatusFlag::BrkExecuted);
	P.Set(StatusFlag::InterruptsOff);

	// Entry point is located at the Reset interrupt location
	PC = m_pRam->Read16(CpuRam::kResetVector);
}

void Cpu::DebuggerPrintOp()
{
	// Print PC
	printf(ADDR_16 "\t", PC);

	// Print instruction in hex
	for (uint16 i = 0; i < 4; ++i)
	{
		if (i < m_pEntry->numBytes)
			printf("%02X", m_pRam->Read8(PC + i));
		else
			printf(" ");
	}
	printf("\t");

	// Print opcode name
	printf("%s ", OpCodeName::String[m_pEntry->opCodeName]);

	// Print operand
	switch (m_pEntry->addrMode)
	{
	case AddressMode::Immedt:
		{
			const uint8 address = m_pRam->Read8(PC+1);
			printf("#" ADDR_8, address);
		}
		break;

	case AddressMode::Implid:
		// No operand to output
		break;

	case AddressMode::Accumu:
		{
			printf("A");
		}
		break;

	case AddressMode::Relatv:
		{
			// For branch instructions, resolve the target address and print it in comments
			const int8 offset = m_pRam->Read8(PC+1); // Signed offset in [-128,127]
			const uint16 target = PC + m_pEntry->numBytes + offset;
			printf(ADDR_8 " ; " ADDR_16 " (%d)", (uint8)offset, target, offset);
		}
		break;

	case AddressMode::ZeroPg:
		{
			const uint8 address = m_pRam->Read8(PC+1);
			printf(ADDR_8, address);
		}
		break;

	case AddressMode::ZPIdxX:
		{
			const uint8 address = m_pRam->Read8(PC+1);
			printf(ADDR_8 ",X", address);
		}
		break;

	case AddressMode::ZPIdxY:
		{
			const uint8 address = m_pRam->Read8(PC+1);
			printf(ADDR_8 ",Y", address);
		}
		break;

	case AddressMode::Absolu:
		{
			uint16 address = m_pRam->Read16(PC+1);
			printf(ADDR_16, address);
		}
		break;

	case AddressMode::AbIdxX:
		{
			uint16 address = m_pRam->Read16(PC+1);
			printf(ADDR_16 ",X", address);
		}
		break;

	case AddressMode::AbIdxY:
		{
			uint16 address = m_pRam->Read16(PC+1);
			printf(ADDR_16 ",Y", address);
		}
		break;

	case AddressMode::Indrct:
		{
			uint16 address = m_pRam->Read16(PC+1);
			printf("(" ADDR_16 ")", address);
		}
		break;

	case AddressMode::IdxInd:
		{
			const uint8 address = m_pRam->Read8(PC+1);
			printf("(" ADDR_8 ",X)", address);
		}
		break;

	case AddressMode::IndIdx:
		{
			const uint8 address = m_pRam->Read8(PC+1);
			printf("(" ADDR_8 "),Y", address);
		}
		break;

	default:
		assert(false && "Invalid addressing mode");
		break;
	}
	
	printf("\n");
}

void Cpu::DebuggerPrintState()
{
	static const char StatusFlagNames[] =
	{
		'C',
		'Z',
		'I',
		'D',
		'B',
		'U',
		'V',
		'N',
	};

	using namespace StatusFlag;

#define HILO(v) (P.Test(v) ? StatusFlagNames[BitFlagToPos<v>::Result-1] : tolower(StatusFlagNames[BitFlagToPos<v>::Result-1]))

	printf("  SP="ADDR_8" A="ADDR_8" X="ADDR_8" Y="ADDR_8" P=[%c%c%c%c%c%c%c%c] ("ADDR_16")="ADDR_8"\n",
		SP, A, X, Y, HILO(Negative), HILO(Overflow), HILO(Unused), HILO(BrkExecuted), HILO(Decimal), HILO(InterruptsOff), HILO(Zero), HILO(Carry),
		m_operandAddress, m_pRam->Read8(m_operandAddress));

#undef HILO

	static bool stepMode = true;
	char key;
	if (stepMode)
	{
		key = System::WaitForKeyPress();
		switch (tolower(key))
		{
		case 'q':
			m_quit = true;
			break;

		case 'g':
			stepMode = false;
			break;
		}
	}
	else if (System::GetKeyPress(key))
	{
		stepMode = true;
	}
}

void Cpu::Run()
{
	//@TODO: For now, Run executes one instruction while implementing PPU
	//while (!m_quit)
	{
		uint8 opCode = m_pRam->Read8(PC);
		m_pEntry = g_ppOpCodeTable[opCode];

		if (m_pEntry == nullptr)
		{
			assert(false && "Unknown opcode");
		}

#if DEBUGGING_ENABLED
		DebuggerPrintOp();
#endif

		UpdateOperand();

		ExecuteInstruction();

#if DEBUGGING_ENABLED
		DebuggerPrintState();
#endif
	}
}

void Cpu::UpdateOperand()
{
	//@TODO: For all 2 byte reads, we need to compute potential page boundary penalty
	
	//@OPT: The first read from memory always reads from code segment, so there's no need to worry
	// about mirroring. Either provide a faster CpuRam::Read func, or just get a pointer to the start
	// of the code segment.

#if CONFIG_DEBUG
	m_operandAddress = 0; // Reset to help find bugs
#endif

	switch (m_pEntry->addrMode)
	{
	case AddressMode::Immedt:
		m_operandAddress = PC + 1; // Set to address of immediate value in code segment
		break;

	case AddressMode::Implid:
		break;

	case AddressMode::Accumu:
		break;

	case AddressMode::Relatv: // For conditional branch instructions
		{
			//@OPT: Lazily compute if branch condition succeeds

			// For branch instructions, resolve the target address
			const int8 offset = m_pRam->Read8(PC+1); // Signed offset in [-128,127]
			m_operandAddress = PC + m_pEntry->numBytes + offset;
		}
		break;

	case AddressMode::ZeroPg:
		m_operandAddress = TO16(m_pRam->Read8(PC+1));
		break;

	case AddressMode::ZPIdxX:
		m_operandAddress = TO16((m_pRam->Read8(PC+1) + X)) & 0x00FF; // Wrap around zero-page boundary
		break;

	case AddressMode::ZPIdxY:
		m_operandAddress = TO16((m_pRam->Read8(PC+1) + Y)) & 0x00FF; // Wrap around zero-page boundary
		break;

	case AddressMode::Absolu:
		m_operandAddress = m_pRam->Read16(PC+1);
		break;

	case AddressMode::AbIdxX:
		m_operandAddress = m_pRam->Read16(PC+1) + X;
		break;

	case AddressMode::AbIdxY:
		m_operandAddress = m_pRam->Read16(PC+1) + Y;
		break;

	case AddressMode::Indrct: // for JMP only
		{
			uint16 indirectAddress1 = m_pRam->Read16(PC+1);

			// Handle the 6502 bug for when the low-byte of the effective address is FF,
			// in which case the 2nd byte read does not correctly cross page boundaries.
			// The bug is that the high byte does not change.
			uint16 indirectAddress2 = (indirectAddress1 & 0xFF00) | ((indirectAddress1 + 1) & 0x00FF);

			m_operandAddress = TO16(m_pRam->Read8(indirectAddress1)) | TO16(m_pRam->Read8(indirectAddress2)) << 8;
		}
		break;

	case AddressMode::IdxInd:
		{
			uint16 indirectAddress = TO16((m_pRam->Read8(PC+1) + X)) & 0x00FF; // Get zero page address of bytes holding address of operand
			m_operandAddress = m_pRam->Read16(indirectAddress); // Get operand address
		}
		break;

	case AddressMode::IndIdx:
		{
			const uint16 indirectAddress = TO16(m_pRam->Read8(PC+1)); // Get zero page address of bytes holding address of operand
			m_operandAddress = m_pRam->Read16(indirectAddress) + Y; // Get operand address
		}
		break;

	default:
		assert(false && "Invalid addressing mode");
		break;
	}
}

void Cpu::ExecuteInstruction()
{
	using namespace OpCodeName;
	using namespace StatusFlag;

	const uint16 startPC = PC;

	switch (m_pEntry->opCodeName)
	{
	case ADC: // Add memory to accumulator with carry
		{
			// Operation:  A + M + C -> A, C
			const uint8 value = GetMemValue();
			const uint16 result = TO16(A) + TO16(value) + TO16(P.Test(Carry));
			P.Set(Negative, CalcNegativeFlag(result));
			P.Set(Zero, CalcZeroFlag(result));
			P.Set(Carry, CalcCarryFlag(result));
			P.Set(Overflow, CalcOverflowFlag(A, value, result));
			A = TO8(result);
		}
		break;

	case AND: // "AND" memory with accumulator
		A &= GetMemValue();
		P.Set(Negative, CalcNegativeFlag(A));
		P.Set(Zero, CalcZeroFlag(A));
		break;

	case ASL: // Shift Left One Bit (Memory or Accumulator)
		{
			const uint16 result = TO16(GetAccumOrMemValue()) << 1;
			P.Set(Negative, CalcNegativeFlag(result));
			P.Set(Zero, CalcZeroFlag(result));
			P.Set(Carry, CalcCarryFlag(result));
			SetAccumOrMemValue(TO8(result));
		}
		break;

	case BCC: // Branch on Carry Clear
		if (!P.Test(Carry))
			PC = GetBranchOrJmpLocation();
		break;

	case BCS: // Branch on Carry Set
		if (P.Test(Carry))
			PC = GetBranchOrJmpLocation();
		break;

	case BEQ: // Branch on result zero (equal means compare difference is 0)
		if (P.Test(Zero))
			PC = GetBranchOrJmpLocation();
		break;

	case BIT: // Test bits in memory with accumulator
		{
			uint8 result = A & GetMemValue();
			P.Set(Negative, result & 0x80); // Bit 7
			P.Set(Overflow, result & 0x40); // Bit 6
			P.Set(Zero, CalcZeroFlag(result));
		}
		break;

	case BMI: // Branch on result minus
		if (P.Test(Negative))
			PC = GetBranchOrJmpLocation();
		break;

	case BNE:  // Branch on result non-zero
		if (!P.Test(Zero))
			PC = GetBranchOrJmpLocation();
		break;

	case BPL: // Branch on result plus
		if (!P.Test(Negative))
			PC = GetBranchOrJmpLocation();
		break;

	case BRK: // Force break (Forced Interrupt PC + 2 toS P toS) (used with RTI)
		{
			uint16 returnAddr = PC + m_pEntry->numBytes;
			Push16(returnAddr);
			P.Set(BrkExecuted); // Set break flag before pushing status register (signifies s/w interrupt)
			Push8(P.Value());
			P.Set(InterruptsOff); // Disable hardware IRQs
			PC = m_pRam->Read16(CpuRam::kIrqVector);
		}
		break;

	case BVC: // Branch on Overflow Clear
		if (!P.Test(Overflow))
			PC = GetBranchOrJmpLocation();
		break;

	case BVS: // Branch on Overflow Set
		if (P.Test(Overflow))
			PC = GetBranchOrJmpLocation();
		break;

	case CLC: // CLC Clear carry flag
		P.Clear(Carry);
		break;

	case CLD: // CLD Clear decimal mode
		P.Clear(Decimal);
		break;

	case CLI: // CLI Clear interrupt disable bit
		P.Clear(InterruptsOff);
		break;

	case CLV: // CLV Clear overflow flag
		P.Clear(Overflow);
		break;

	case CMP: // CMP Compare memory and accumulator
		{
			const uint16 result = TO16(A) - TO16(GetMemValue());
			P.Set(Negative, CalcNegativeFlag(result));
			P.Set(Zero, CalcZeroFlag(result));
			P.Set(Carry, CalcCarryFlag(result));
		}
		break;

	case CPX: // CPX Compare Memory and Index X
		{
			uint16 result = TO16(X) - TO16(GetMemValue());
			P.Set(Negative, CalcNegativeFlag(result));
			P.Set(Zero, CalcZeroFlag(result));
			P.Set(Carry, CalcCarryFlag(result));
		}
		break;

	case CPY: // CPY Compare memory and index Y
		{
			const uint16 result = TO16(Y) - TO16(GetMemValue());
			P.Set(Negative, CalcNegativeFlag(result));
			P.Set(Zero, CalcZeroFlag(result));
			P.Set(Carry, CalcCarryFlag(result));
		}
		break;

	case DEC: // Decrement memory by one
		{
			const uint8 result = GetMemValue() - 1;
			P.Set(Negative, CalcNegativeFlag(result));
			P.Set(Zero, CalcZeroFlag(result));
			SetMemValue(result);
		}
		break;

	case DEX: // Decrement index X by one
		--X;
		P.Set(Negative, CalcNegativeFlag(X));
		P.Set(Zero, CalcZeroFlag(X));
		break;

	case DEY: // Decrement index Y by one
		--Y;
		P.Set(Negative, CalcNegativeFlag(Y));
		P.Set(Zero, CalcZeroFlag(Y));
		break;

	case EOR: // "Exclusive-Or" memory with accumulator
		A = A ^ GetMemValue();
		P.Set(Negative, CalcNegativeFlag(A));
		P.Set(Zero, CalcZeroFlag(A));
		break;

	case INC: // Increment memory by one
		{
			const uint8 result = GetMemValue() + 1;
			P.Set(Negative, CalcNegativeFlag(result));
			P.Set(Zero, CalcZeroFlag(result));
			SetMemValue(result);
		}
		break;

	case INX: // Increment Index X by one
		++X;
		P.Set(Negative, CalcNegativeFlag(X));
		P.Set(Zero, CalcZeroFlag(X));
		break;

	case INY: // Increment Index Y by one
		++Y;
		P.Set(Negative, CalcNegativeFlag(Y));
		P.Set(Zero, CalcZeroFlag(Y));
		break;

	case JMP: // Jump to new location
		PC = GetBranchOrJmpLocation();
		break;

	case JSR: // Jump to subroutine (used with RTS)
		{
			// JSR actually pushes address of the next instruction - 1.
			// RTS jumps to popped value + 1.
			const uint16 returnAddr = PC + m_pEntry->numBytes - 1;
			Push16(returnAddr);
			PC = GetBranchOrJmpLocation();
		}
		break;

	case LDA: // Load accumulator with memory
		A = GetMemValue();
		P.Set(Negative, CalcNegativeFlag(A));
		P.Set(Zero, CalcZeroFlag(A));
		break;

	case LDX: // Load index X with memory
		X = GetMemValue();
		P.Set(Negative, CalcNegativeFlag(X));
		P.Set(Zero, CalcZeroFlag(X));
		break;

	case LDY: // Load index Y with memory
		Y = GetMemValue();
		P.Set(Negative, CalcNegativeFlag(Y));
		P.Set(Zero, CalcZeroFlag(Y));
		break;

	case LSR: // Shift right one bit (memory or accumulator)
		{
			const uint8 value = GetAccumOrMemValue();
			const uint8 result = value >> 1;
			P.Set(Carry, value & 0x01); // Will get shifted into carry
			P.Set(Zero, CalcZeroFlag(result));
			P.Clear(Negative); // 0 is shifted into sign bit position
		}		
		break;

	case NOP: // No Operation (2 cycles)
		break;

	case ORA: // "OR" memory with accumulator
		A |= GetMemValue();
		P.Set(Negative, CalcNegativeFlag(A));
		P.Set(Zero, CalcZeroFlag(A));
		break;

	case PHA: // Push accumulator on stack
		Push8(A);
		break;

	case PHP: // Push processor status on stack
		P.Set(BrkExecuted); // Set break flag before pushing status register (signifies s/w interrupt)
		Push8(P.Value());
		break;

	case PLA: // Pull accumulator from stack
		A = Pop8();
		P.Set(Negative, CalcNegativeFlag(A));
		P.Set(Zero, CalcZeroFlag(A));
		break;

	case PLP: // Pull processor status from stack
		P.Set(Pop8());
		assert(P.Test(BrkExecuted)); // Should be set since we set it on PHP
		break;

	case ROL: // Rotate one bit left (memory or accumulator)
		{
			const uint16 result = (TO16(GetAccumOrMemValue()) << 1) | TO16(P.Test(Carry));
			P.Set(Carry, CalcCarryFlag(result));
			P.Set(Negative, CalcNegativeFlag(result));
			P.Set(Zero, CalcZeroFlag(result));
			SetAccumOrMemValue(TO8(result));
		}
		break;

	case ROR: // Rotate one bit right (memory or accumulator)
		{
			const uint8 value = GetAccumOrMemValue();
			const uint8 result = (value >> 1) & (P.Test(Carry) << 7);
			P.Set(Carry, value & 0x01);
			P.Set(Negative, CalcNegativeFlag(result));
			P.Set(Zero, CalcZeroFlag(result));
			SetAccumOrMemValue(result);
		}
		break;

	case RTI: // Return from interrupt (used with BRK)
		{
			P.Set(Pop8());
			PC = Pop16();
		}
		break;

	case RTS: // Return from subroutine (used with JSR)
		{
			PC = Pop16() + 1;
		}
		break;

	case SBC: // Subtract memory from accumulator with borrow
		{
			// Operation:  A - M - C -> A
			const uint8 value = -GetMemValue(); // We negate the value so we can add. This works because the carry is set when borrowing.
			const uint16 result = TO16(A) + TO16(value) + TO16(P.Test(Carry));
			P.Set(Negative, CalcNegativeFlag(result));
			P.Set(Zero, CalcZeroFlag(result));
			P.Set(Carry, CalcCarryFlag(result));
			P.Set(Overflow, CalcOverflowFlag(A, value, result));
			A = TO8(result);
		}
		break;

	case SEC: // Set carry flag
		P.Set(Carry);
		break;

	case SED: // Set decimal mode
		P.Set(Decimal);
		break;

	case SEI: // Set interrupt disable status
		P.Set(InterruptsOff);
		break;

	case STA: // Store accumulator in memory
		SetMemValue(A);
		break;

	case STX: // Store index X in memory
		SetMemValue(X);
		break;

	case STY: // Store index Y in memory
		SetMemValue(Y);
		break;

	case TAX: // Transfer accumulator to index X
		X = A;
		P.Set(Negative, CalcNegativeFlag(X));
		P.Set(Zero, CalcZeroFlag(X));
		break;

	case TAY: // Transfer accumulator to index Y
		Y = A;
		P.Set(Negative, CalcNegativeFlag(Y));
		P.Set(Zero, CalcZeroFlag(Y));
		break;

	case TSX: // Transfer stack pointer to index X
		X = SP;
		P.Set(Negative, CalcNegativeFlag(X));
		P.Set(Zero, CalcZeroFlag(X));
		break;

	case TXA: // Transfer index X to accumulator
		A = X;
		P.Set(Negative, CalcNegativeFlag(A));
		P.Set(Zero, CalcZeroFlag(A));
		break;

	case TXS: // Transfer index X to stack pointer
		SP = X;
		break;

	case TYA: // Transfer index Y to accumulator
		A = Y;
		P.Set(Negative, CalcNegativeFlag(A));
		P.Set(Zero, CalcZeroFlag(A));
		break;
	}

	// If instruction hasn't modified PC, move it to next instruction
	if (startPC == PC)
		PC += m_pEntry->numBytes;
}

uint8 Cpu::GetAccumOrMemValue() const
{
	assert(m_pEntry->addrMode == AddressMode::Accumu || m_pEntry->addrMode & AddressMode::MemoryValueOperand);

	if (m_pEntry->addrMode == AddressMode::Accumu)
		return A;
	
	uint8 result = m_pRam->Read8(m_operandAddress);
	m_pNes->OnCpuMemoryRead(result);
	return result;
}

void Cpu::SetAccumOrMemValue(uint8 value)
{
	assert(m_pEntry->addrMode == AddressMode::Accumu || m_pEntry->addrMode & AddressMode::MemoryValueOperand);

	if (m_pEntry->addrMode == AddressMode::Accumu)
	{
		A = value;
	}
	else
	{
		m_pRam->Write8(m_operandAddress, value);
		m_pNes->OnCpuMemoryWrite(m_operandAddress);
	}
}

uint8 Cpu::GetMemValue() const
{
	assert(m_pEntry->addrMode & AddressMode::MemoryValueOperand);
	uint8 result = m_pRam->Read8(m_operandAddress);
	m_pNes->OnCpuMemoryRead(result);
	return result;
}

void Cpu::SetMemValue(uint8 value)
{
	assert(m_pEntry->addrMode & AddressMode::MemoryValueOperand);
	m_pRam->Write8(m_operandAddress, value);
	m_pNes->OnCpuMemoryWrite(m_operandAddress);
}

uint16 Cpu::GetBranchOrJmpLocation() const
{
	assert(m_pEntry->addrMode & AddressMode::JmpOrBranchOperand);
	return m_operandAddress;
}

void Cpu::Push8(uint8 value)
{
	m_pRam->Write8(CpuRam::kStackBase + SP, value);
	--SP;
}

void Cpu::Push16(uint16 value)
{
	Push8(value >> 8);
	Push8(value & 0x00FF);
}

uint8 Cpu::Pop8()
{
	++SP;
	return m_pRam->Read8(CpuRam::kStackBase + SP);
}

uint16 Cpu::Pop16()
{
	return TO16(Pop8()) | TO16(Pop8()) << 8;
}

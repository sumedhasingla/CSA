#include "iostream"
#include "fstream"
#include "string"
#include "map"
#include "vector"
#include "array"

#define IntegerIndex 0
#define DividerIndex 1
#define MultiplierIndex 2
#define LoadIndex 3
#define StoreIndex 4
#define FUType 5 //Number of Functional Unit type

using namespace std;

enum stage { Issue, Read, Execute, Write, Wait };
enum stall { StructuralHazard, WaitingForOperand, WaitingForFunctionalUnit };
int numberOfStructuralHazardStalls = 0;
int totalNumberOfClockCycles = 0;
int numberOfOperandReadFromRegisterFile = 0;

//structure
struct reservationStation {
	bool busy = false;
	bool source1Ready = false;
	bool source2Ready = false;
	float source1Value;
	float source2Value;
	int source1Producer[2] = { -1,-1 };
	int source2Producer[2] = { -1, -1 };
	int destination[2];
};

struct functionalUnit {
	bool busy = false;
	int reservationStationNumber;
	int numberOfInstructionsExecuted = 0;
};

struct instruction {
	int PipelineStage;
	int WaitCode = -1; //If instruction is in "WAIT" stage, then wait code tells why the instruction is waiting
	int FunctionalUnitType; //index
	int FunctionalUnit = -1; //the number of FU
	int ReservationStation = -1; // the number of RS alloted to this instruction
	int CCExecutionStarted = -1; //Clock cycle number when the execution stage started for this instruction
	int CCpassed = 0; //number of CC the current instruction has executed so far. When this number becomes equal to FU latency, the instruction has completed its execution
	std::vector<int> inst; //program instruction
};

//array of reservation_station
std::array< std::vector<reservationStation> , FUType> ReservationStations;

//array of functional unit
std::array< std::vector<functionalUnit> , FUType> FunctionalUnits;

//array of clock cycles
int ClockCycles[FUType];

//16 bit registers
signed short registers[8] = { 0 };

//HashMap Key: register_Number Value: [type of functional unit, reservation station number]
std::map< int, std::array<int,2> > registerResultStatus;

//vector of instructions
/** each instruction is saves in the form of an array of integers
	R format instruction
	int[0]: index (tells which type of function unit is required by this instruction
		0: Integer
		1: Divider
		2: Multiplier
		3: Load
		4: Store
	int[1]: Rd, destination register number (destination)
	int[2]: Rs, source register number (source1)
	int[3]: Rt, target register number (source2)	
	I format instruction
	int[0]: index
	int[1]: Rd
	X format instruction
	int[0]: index
**/
std::vector< std::vector<int> > inputInstructions;

//vector of active instructions. The active instructions are the one which are currently in some stage in pipeline.
std::vector< instruction > activeInstructions;

//HashMap Key: opcode Value: Index representing type of functinal unit required by this opcode
std::map<unsigned char, int> opcodeIndex;

//Initialize the simulator
//returns true if simulator is initialize properly
bool initializeSimulator()
{
	opcodeIndex[0] = IntegerIndex; //Add: Integer FU
	opcodeIndex[1] = IntegerIndex; //Sub: Integer FU
	opcodeIndex[2] = IntegerIndex; //And: Integer FU
	opcodeIndex[3] = IntegerIndex; //Nor: Integer FU
	opcodeIndex[4] = DividerIndex; //Div: Divider FU
	opcodeIndex[5] = MultiplierIndex; //Mul: Multiplier FU
	opcodeIndex[6] = DividerIndex; //Mod: Divider FU
	opcodeIndex[7] = DividerIndex; //Exp: Divider FU
	opcodeIndex[8] = LoadIndex; //lw: LOAD FU
	opcodeIndex[9] = StoreIndex; //sw: STORE FU
	opcodeIndex[16] = IntegerIndex; //liz: Integer FU
	opcodeIndex[17] = IntegerIndex; //lis: Integer FU
	opcodeIndex[18] = IntegerIndex; //lui: Integer FU
	opcodeIndex[13] = IntegerIndex; //halt: Integer FU
	opcodeIndex[14] = IntegerIndex; //put: Integer FU

	return true;
}

bool readConfigFile(std::string fileName)
{
	string line;
	ifstream configFile(fileName);
	if (configFile.is_open())
	{
		while (getline(configFile, line))
		{
			if ((line.length() > 0) && line[0] != '#')
			{
				std::size_t start = line.find("\"") + 1;
				std::size_t end = line.find("\":");
				std::string key = line.substr(start, end - start);				
				line = line.substr(end + 2);
				start = line.find("\":") + 2;
				end = line.find(",");
				std::string value1 = line.substr(start, end - start);
				line = line.substr(end+1);
				start = line.find("\":") + 2;
				end = line.find(",");
				std::string value2 = line.substr(start, end - start);
				line = line.substr(end);
				start = line.find("\":") + 2;
				end = line.find("}");
				std::string value3 = line.substr(start, end - start);

				//cout << key << "\t" << std::stoi(value1) << "\t" << std::stoi(value2) << "\t" << std::stoi(value3) << endl;
				int index = -1;
				if (key == "integer")
				{
					index = IntegerIndex;
				}
				else if (key == "divider")
				{
					index = DividerIndex;
				}
				else if (key == "multiplier")
				{
					index = MultiplierIndex;
				}
				else if (key == "load")
				{
					index = LoadIndex;
				}
				else if (key == "store")
				{
					index = StoreIndex;
				}
				else
				{
					cout << "Invalid key in configuration file: " << key << endl;
					return false;
				}
				int FU = std::stoi(value1);
				int RS = std::stoi(value2);
				int CC = std::stoi(value3);
				
				//Allocate FU
				for (int i = 0; i < FU; i++)
				{
					functionalUnit f;					
					FunctionalUnits[index].push_back(f);
				}

				//Allocate Reservation Station
				for (int i = 0; i < RS; i++)
				{
					reservationStation r;
					ReservationStations[index].push_back(r);
				}

				ClockCycles[index] = CC;
			}
		}
		return true;
	}
	else
	{
		cout << "Cannot read the config file";
		return false;
	}
}

bool readTraceFile(std::string fileName)
{
	string line;
	ifstream programFile(fileName);
	if (programFile.is_open())
	{
		while (getline(programFile, line))
		{
			if ((line.length() > 0)  && line[0] != '#')
			{
				std::vector<int> currentInstruction;
				unsigned short instInt = std::stoi(line, nullptr, 16);
				unsigned char lowerOrderBits = instInt & 0xff;
				unsigned char higherOrderBits = instInt >> 8;
				unsigned char opcode = higherOrderBits >> 3;
				if (opcodeIndex.find(opcode) == opcodeIndex.end()) //if the instruction is not using any of the known FU, ignore it
					continue;
				currentInstruction.push_back(opcodeIndex[opcode]); //index
				if (opcode >= 0 && opcode <= 7)
				{
					currentInstruction.push_back( higherOrderBits & 7 );  // bit[10-8] destinationRegister
					currentInstruction.push_back( lowerOrderBits >> 5 ); //bit[7-5] sourceRegister
					currentInstruction.push_back( (lowerOrderBits >> 2) & 7 ); //bit[4-2] targetRegister
				}
				else if (opcode == 8) //load
				{
					currentInstruction.push_back( higherOrderBits & 7 );  // bit[10-8] destinationRegister
					currentInstruction.push_back( lowerOrderBits >> 5 ); //bit[7-5] sourceRegister
				}
				else if (opcode == 9) //store
				{
					currentInstruction.push_back( (lowerOrderBits >> 2) & 7 ); //bit[4-2] targetRegister
					currentInstruction.push_back( lowerOrderBits >> 5 ); //bit[7-5] sourceRegister
				}
				else if (opcode >= 16 && opcode <= 17)
				{
					currentInstruction.push_back( higherOrderBits & 7 );  // bit[10-8] destinationRegister
				}
				else if (opcode == 18) //lui
				{
					currentInstruction.push_back(higherOrderBits & 7);  // bit[10-8] destinationRegister
					currentInstruction.push_back(higherOrderBits & 7);  // bit[10-8] destinationRegister
				}
				else if (opcode == 14)// put
				{
					currentInstruction.push_back( lowerOrderBits >> 5 ); //bit[7-5] sourceRegister
					currentInstruction.push_back(lowerOrderBits >> 5); //bit[7-5] sourceRegister
					currentInstruction.push_back(lowerOrderBits >> 5); //bit[7-5] sourceRegister
					currentInstruction.push_back(lowerOrderBits >> 5); //bit[7-5] sourceRegister
				}
				else if (opcode == 13)//halt
				{}
				else
				{
					cout << "Error: Invalid opcode";
					return false;
				}
				//cout << currentInstruction[0] << "\t" << currentInstruction[1] << "\t" << currentInstruction[2] << "\t" << currentInstruction[3] << endl;
				inputInstructions.push_back(currentInstruction);
			}
		}
		programFile.close();
		return true;
	}
	else
	{
		cout << "Cannot read the input file";
		return false;
	}
}


//Print functions for debugging
void printInputInstructions()
{
	int length = inputInstructions.size();
	for (int i = 0; i < length; i++)
	{
		int length1 = inputInstructions[i].size();
		switch (inputInstructions[i][0])
		{
		case IntegerIndex:
			cout << "Integer" << "\t";
			break;
		case MultiplierIndex:
			cout << "Multiplier" << "\t";
			break;
		case DividerIndex:
			cout << "Divider" << "\t";
			break;
		case LoadIndex:
			cout << "Load" << "\t";
			break;
		case StoreIndex:
			cout << "Store" << "\t";
			break;
		}
		for (int j = 1; j < length1; j++)
		{
			cout << inputInstructions[i][j] << "\t";
		}
		cout << endl;
	}
}

void printReservationStations()
{
	for (int i = 0; i < FUType; i++)
	{
		switch (i)
		{
		case IntegerIndex:
			cout << "Integer Reservation Stations" << ".............\n";
			break;
		case MultiplierIndex:
			cout << "Multiplier Reservation Stations" << ".............\n";
			break;
		case DividerIndex:
			cout << "Divider Reservation Stations" << ".......\n";
			break;
		case LoadIndex:
			cout << "Load Reservation Stations" << ".........\n";
			break;
		case StoreIndex:
			cout << "Store Reservation Stations" << "........\n";
			break;
		}
		int count = ReservationStations[i].size();
		for (int j = 0; j < count; j++)
		{
			cout << "Reservation Station Number: " << j + 1 << endl;
			cout << "Status: " << ReservationStations[i][j].busy << endl;
			if (ReservationStations[i][j].source1Ready)
			{
				cout << "Source1 is ready " << endl;
			}
			else
			{
				cout << "Source1 is waiting for Functional Unit Index: " << ReservationStations[i][j].source1Producer[0] <<
					" and Reservation Station: " << ReservationStations[i][j].source1Producer[1] << endl;
			}
			if (ReservationStations[i][j].source2Ready)
			{
				cout << "Source2 is ready " << endl;
			}
			else
			{
				cout << "Source2 is waiting for Functional Unit Index: " << ReservationStations[i][j].source2Producer[0] <<
					" and Reservation Station: " << ReservationStations[i][j].source2Producer[1] << endl;
			}
		}
		cout << endl << endl;
	}
}

void printFunctionalUnits()
{
	for (int i = 0; i < FUType; i++)
	{
		switch (i)
		{
		case IntegerIndex:
			cout << "Integer FU" << ".............\n";
			break;
		case MultiplierIndex:
			cout << "Multiplier FU" << ".............\n";
			break;
		case DividerIndex:
			cout << "Divider FU" << ".......\n";
			break;
		case LoadIndex:
			cout << "Load FU" << ".........\n";
			break;
		case StoreIndex:
			cout << "Store FU" << "........\n";
			break;
		}
		int count = FunctionalUnits[i].size();
		for (int j = 0; j < count; j++)
		{
			cout << "FU Number: " << j + 1 << endl;
			cout << "Status: " << FunctionalUnits[i][j].busy << endl;
			if (FunctionalUnits[i][j].busy)
			{
				cout << "RS using this FU: " << FunctionalUnits[i][j].reservationStationNumber << endl;
			}
		}
		cout << endl << endl;
	}
}

void printRegisterStatus()
{
	for (std::map< int, std::array<int, 2> >::iterator it = registerResultStatus.begin(); it != registerResultStatus.end(); ++it)
	{
		std::cout << "Register No.: " << it->first << " depends on the functional unit type: " << it->second[0] << " Reservation Station: " << it->second[1] << '\n';
	}
}


// Functions to simulate each stage in pipeline
//Function returns true if everything runs smoothly, else false
bool IssueInstruction( int indexActiveInstruction )
{
	//current instruction is activeInstructions[indexActiveInstruction]
	if (activeInstructions.size() <= indexActiveInstruction)
	{
		cout << "Error: Error while calling IssueInstruction()" << endl;
		return false;
	}
	//check which type of Functional Unit is required by this instruction
	int typeFU = activeInstructions[indexActiveInstruction].FunctionalUnitType;

	//check if any reservation station of this type is available
	int count = ReservationStations[typeFU].size();
	for (int i = 0; i < count; i++)
	{
		if (ReservationStations[typeFU][i].busy == false)
		{
			//Allocate Reservation Station
			ReservationStations[typeFU][i].busy = true;
			activeInstructions[indexActiveInstruction].ReservationStation = i;
			activeInstructions[indexActiveInstruction].PipelineStage = Read;
			return true;
		}		
	}
	//If no Reservation Station of required type is available, stall the instruction
	activeInstructions[indexActiveInstruction].PipelineStage = Wait;
	activeInstructions[indexActiveInstruction].WaitCode = StructuralHazard;
	numberOfStructuralHazardStalls += 1;
	return true;
}

bool ReadOperands( int indexActiveInstruction )
{
	//current instruction is activeInstructions[indexActiveInstruction]
	if (activeInstructions.size() <= indexActiveInstruction)
	{
		cout << "Error: Error while calling ReadOperands()" << endl;
		return false;
	}
	//check which type of Functional Unit is required by this instruction
	int typeFU = activeInstructions[indexActiveInstruction].FunctionalUnitType;
	//get the number of RS alloted to this instruction
	int RS = activeInstructions[indexActiveInstruction].ReservationStation;
	if (RS == -1)
	{
		cout << "Error: A reservation station must be assigned before reaching the Read stage" << endl;
		return false;
	}
	//Update the reservation station status
	std::array<int, 2> temp;
	temp[0] = typeFU;
	temp[1] = RS;
	ReservationStations[typeFU][RS].destination[0] = typeFU;
	ReservationStations[typeFU][RS].destination[1] = RS;
	int instructionSize = activeInstructions[indexActiveInstruction].inst.size(); //helps identify what type of instruction is this
	if (instructionSize == 4) //Its a reg reg instruction with 3 registers // add, sub, and, nor, div, mul, mod, exp
	{
		int destinationRegister = activeInstructions[indexActiveInstruction].inst[1];
		int source1 = activeInstructions[indexActiveInstruction].inst[2];
		int source2 = activeInstructions[indexActiveInstruction].inst[3];
		if (registerResultStatus.find(source1) != registerResultStatus.end()) //source1 is destination of some active instruction
		{
			ReservationStations[typeFU][RS].source1Ready = false;
			ReservationStations[typeFU][RS].source1Producer[0] = registerResultStatus[source1][0];
			ReservationStations[typeFU][RS].source1Producer[1] = registerResultStatus[source1][1];
			//Since operand is not ready, the instruction should go in Wait stage
			activeInstructions[indexActiveInstruction].PipelineStage = Wait;
			activeInstructions[indexActiveInstruction].WaitCode = WaitingForOperand;
		}
		else
		{
			ReservationStations[typeFU][RS].source1Ready = true;
			// We will read the value from register file
			numberOfOperandReadFromRegisterFile += 1;
		}
		if (registerResultStatus.find(source2) != registerResultStatus.end()) //source2 is destination of some active instruction
		{
			ReservationStations[typeFU][RS].source2Ready = false;
			ReservationStations[typeFU][RS].source2Producer[0] = registerResultStatus[source2][0];
			ReservationStations[typeFU][RS].source2Producer[1] = registerResultStatus[source2][1];
			//Since operand is not ready, the instruction should go in Wait stage
			activeInstructions[indexActiveInstruction].PipelineStage = Wait;
			activeInstructions[indexActiveInstruction].WaitCode = WaitingForOperand;
		}
		else
		{
			ReservationStations[typeFU][RS].source2Ready = true;
			// We will read the value from register file
			numberOfOperandReadFromRegisterFile += 1;
		}
		if (ReservationStations[typeFU][RS].source1Ready == true && ReservationStations[typeFU][RS].source2Ready == true)
		{
			//both the operands are ready, we can now go to execute stage
			activeInstructions[indexActiveInstruction].PipelineStage = Execute;
		}
		registerResultStatus[destinationRegister] = temp;
	}
	else if (typeFU == IntegerIndex && instructionSize == 2) //liz, lis
	{
		ReservationStations[typeFU][RS].source1Ready = true;
		ReservationStations[typeFU][RS].source2Ready = true;
		int destinationRegister = activeInstructions[indexActiveInstruction].inst[1];
		registerResultStatus[destinationRegister] = temp;
		//both the operands are ready, we can now go to execute stage
		activeInstructions[indexActiveInstruction].PipelineStage = Execute;
	}
	else if (typeFU == LoadIndex) //load $rd, $rs
	{
		ReservationStations[typeFU][RS].source2Ready = true;
		int destinationRegister = activeInstructions[indexActiveInstruction].inst[1];
		int source1 = activeInstructions[indexActiveInstruction].inst[2];
		if (registerResultStatus.find(source1) != registerResultStatus.end()) //source1 is destination of some active instruction
		{
			ReservationStations[typeFU][RS].source1Ready = false;
			ReservationStations[typeFU][RS].source1Producer[0] = registerResultStatus[source1][0];
			ReservationStations[typeFU][RS].source1Producer[1] = registerResultStatus[source1][1];
			//Since operand is not ready, the instruction should go in Wait stage
			activeInstructions[indexActiveInstruction].PipelineStage = Wait;
			activeInstructions[indexActiveInstruction].WaitCode = WaitingForOperand;
		}
		else
		{
			ReservationStations[typeFU][RS].source1Ready = true;
			// We will read the value from register file
			numberOfOperandReadFromRegisterFile += 1;
			//both the operands are ready, we can now go to execute stage
			activeInstructions[indexActiveInstruction].PipelineStage = Execute;
		}
		ReservationStations[typeFU][RS].source2Ready = true;
		registerResultStatus[destinationRegister] = temp;
	}
	else if (typeFU == StoreIndex) //store $rt,$rs
	{
		int source1 = activeInstructions[indexActiveInstruction].inst[1];
		int source2 = activeInstructions[indexActiveInstruction].inst[2];
		if (registerResultStatus.find(source1) != registerResultStatus.end()) //source1 is destination of some active instruction
		{
			ReservationStations[typeFU][RS].source1Ready = false;
			ReservationStations[typeFU][RS].source1Producer[0] = registerResultStatus[source1][0];
			ReservationStations[typeFU][RS].source1Producer[1] = registerResultStatus[source1][1];
			//Since operand is not ready, the instruction should go in Wait stage
			activeInstructions[indexActiveInstruction].PipelineStage = Wait;
			activeInstructions[indexActiveInstruction].WaitCode = WaitingForOperand;
		}
		else
		{
			ReservationStations[typeFU][RS].source1Ready = true;
			// We will read the value from register file
			numberOfOperandReadFromRegisterFile += 1;
		}
		if (registerResultStatus.find(source2) != registerResultStatus.end()) //source2 is destination of some active instruction
		{
			ReservationStations[typeFU][RS].source2Ready = false;
			ReservationStations[typeFU][RS].source2Producer[0] = registerResultStatus[source2][0];
			ReservationStations[typeFU][RS].source2Producer[1] = registerResultStatus[source2][1];
			//Since operand is not ready, the instruction should go in Wait stage
			activeInstructions[indexActiveInstruction].PipelineStage = Wait;
			activeInstructions[indexActiveInstruction].WaitCode = WaitingForOperand;
		}
		else
		{
			ReservationStations[typeFU][RS].source2Ready = true;
			// We will read the value from register file
			numberOfOperandReadFromRegisterFile += 1;
		}
		if (ReservationStations[typeFU][RS].source1Ready == true && ReservationStations[typeFU][RS].source2Ready == true)
		{
			//both the operands are ready, we can now go to execute stage
			activeInstructions[indexActiveInstruction].PipelineStage = Execute;
		}
	}
	else if (typeFU == IntegerIndex && instructionSize == 1) //halt
	{
		ReservationStations[typeFU][RS].source1Ready = true;
		ReservationStations[typeFU][RS].source2Ready = true;
		//both the operands are ready, we can now go to execute stage
		activeInstructions[indexActiveInstruction].PipelineStage = Execute;
	}
	else if (typeFU == IntegerIndex && instructionSize == 3) // lui
	{
		int source1 = activeInstructions[indexActiveInstruction].inst[2];
		if (registerResultStatus.find(source1) != registerResultStatus.end()) //source1 is destination of some active instruction
		{
			ReservationStations[typeFU][RS].source1Ready = false;
			ReservationStations[typeFU][RS].source1Producer[0] = registerResultStatus[source1][0];
			ReservationStations[typeFU][RS].source1Producer[1] = registerResultStatus[source1][1];
			//Since operand is not ready, the instruction should go in Wait stage
			activeInstructions[indexActiveInstruction].PipelineStage = Wait;
			activeInstructions[indexActiveInstruction].WaitCode = WaitingForOperand;
		}
		else
		{
			ReservationStations[typeFU][RS].source1Ready = true;
			// We will read the value from register file
			numberOfOperandReadFromRegisterFile += 1;
		}
		ReservationStations[typeFU][RS].source2Ready = true;		
		if (ReservationStations[typeFU][RS].source1Ready == true && ReservationStations[typeFU][RS].source2Ready == true)
		{
			//both the operands are ready, we can now go to execute stage
			activeInstructions[indexActiveInstruction].PipelineStage = Execute;
		}
		int destinationRegister = activeInstructions[indexActiveInstruction].inst[1];
		registerResultStatus[destinationRegister] = temp;
	}
	else if (typeFU == IntegerIndex && instructionSize == 5) // put
	{
		int source1 = activeInstructions[indexActiveInstruction].inst[1];
		if (registerResultStatus.find(source1) != registerResultStatus.end()) //source1 is destination of some active instruction
		{
			ReservationStations[typeFU][RS].source1Ready = false;
			ReservationStations[typeFU][RS].source1Producer[0] = registerResultStatus[source1][0];
			ReservationStations[typeFU][RS].source1Producer[1] = registerResultStatus[source1][1];
			//Since operand is not ready, the instruction should go in Wait stage
			activeInstructions[indexActiveInstruction].PipelineStage = Wait;
			activeInstructions[indexActiveInstruction].WaitCode = WaitingForOperand;
		}
		else
		{
			ReservationStations[typeFU][RS].source1Ready = true;
			// We will read the value from register file
			numberOfOperandReadFromRegisterFile += 1;
			activeInstructions[indexActiveInstruction].PipelineStage = Execute;
		}
	}
	else
	{
		cout << "Error: Some Problem in ReadOperand() function." << endl;
		return false;
	}
	return true;
}

bool ExecuteInstruction(int indexActiveInstruction, int CC)
{
	//current instruction is activeInstructions[indexActiveInstruction]
	if (activeInstructions.size() <= indexActiveInstruction)
	{
		cout << "Error: Error while calling ExecuteInstruction()" << endl;
		return false;
	}
	//check which type of Functional Unit is required by this instruction
	int typeFU = activeInstructions[indexActiveInstruction].FunctionalUnitType;
	//get the number of RS alloted to this instruction
	int RS = activeInstructions[indexActiveInstruction].ReservationStation;
	if (RS == -1)
	{
		cout << "Error: A reservation station must be assigned before reaching the Execute stage" << endl;
		return false;
	}
	//get the number of FU alloted to this instruction
	int FU = activeInstructions[indexActiveInstruction].FunctionalUnit;
	if (FU == -1)
	{
		// no functional unit is assigned as of now
		//check if there is an avialable functional unit
		int count = FunctionalUnits[typeFU].size();
		int i;
		for (i = 0; i < count; i++)
		{
			if (FunctionalUnits[typeFU][i].busy == false)
			{
				//we have found an avilable FU
				activeInstructions[indexActiveInstruction].PipelineStage = Execute;
				FunctionalUnits[typeFU][i].busy = true;
				FunctionalUnits[typeFU][i].reservationStationNumber = RS;
				FunctionalUnits[typeFU][i].numberOfInstructionsExecuted += 1;
				activeInstructions[indexActiveInstruction].FunctionalUnit = i;
				activeInstructions[indexActiveInstruction].CCExecutionStarted = CC; //execution started at this CC
				break;
			}
		}
		if (i == count)
		{
			//there was no available Function Unit
			activeInstructions[indexActiveInstruction].PipelineStage = Wait;
			activeInstructions[indexActiveInstruction].WaitCode = WaitingForFunctionalUnit;
			return true;
		}
	}
	// we have a functional unit, execute the FU for this cycle
	activeInstructions[indexActiveInstruction].CCpassed += 1;

	//check if we complete execution after this cycle
	if (activeInstructions[indexActiveInstruction].CCpassed == ClockCycles[typeFU])
	{
		// instruction have complete the execution, next stage is Write
		activeInstructions[indexActiveInstruction].PipelineStage = Write;
	}
	//else the stage continued in execution stage
	return true;
}

bool WriteBackStage1(int indexActiveInstruction)
{
	//current instruction is activeInstructions[indexActiveInstruction]
	if (activeInstructions.size() <= indexActiveInstruction)
	{
		cout << "Error: Error while calling WriteBackStage1()" << endl;
		return false;
	}
	//check which type of Functional Unit is required by this instruction
	int typeFU = activeInstructions[indexActiveInstruction].FunctionalUnitType;
	//get the number of RS alloted to this instruction
	int RS = activeInstructions[indexActiveInstruction].ReservationStation;
	if (RS == -1)
	{
		cout << "Error: A reservation station must be assigned before reaching the Write stage" << endl;
		return false;
	}
	//get the number of FU alloted to this instruction
	int FU = activeInstructions[indexActiveInstruction].FunctionalUnit;
	if (FU == -1)
	{
		cout << "Error: A Function Unit must be assigned before reaching the Write stage" << endl;
		return false;
	}
	//get the destination value for the current instruction
	//this is the value that the current instruction has produced
	int destFUType = ReservationStations[typeFU][RS].destination[0];
	int destRS = ReservationStations[typeFU][RS].destination[1];
	//check if any of the active instructions required this produced value
	int count = activeInstructions.size();
	for (int i = 0; i < count; i++)
	{	
		if (i != indexActiveInstruction)
		{
			int currentFUType = activeInstructions[i].FunctionalUnitType;
			int currentRS = activeInstructions[i].ReservationStation;
			//check if its source1 is not ready and have producer same as current
			if (currentRS != -1 && ReservationStations[currentFUType][currentRS].source1Ready == false && ReservationStations[currentFUType][currentRS].source1Producer[0] == destFUType && ReservationStations[currentFUType][currentRS].source1Producer[1] == destRS)
			{
				ReservationStations[currentFUType][currentRS].source1Ready = true;
			}
			//check if its source2 is not ready and have producer same as current
			if (currentRS != -1 && ReservationStations[currentFUType][currentRS].source2Ready == false && ReservationStations[currentFUType][currentRS].source2Producer[0] == destFUType && ReservationStations[currentFUType][currentRS].source2Producer[1] == destRS)
			{
				ReservationStations[currentFUType][currentRS].source2Ready = true;
			}
		}
	}
	return true;
}

bool WriteBackStage2(int indexActiveInstruction)
{
	//current instruction is activeInstructions[indexActiveInstruction]
	if (activeInstructions.size() <= indexActiveInstruction)
	{
		cout << "Error: Error while calling WriteBackStage2()" << endl;
		return false;
	}
	//check which type of Functional Unit is required by this instruction
	int typeFU = activeInstructions[indexActiveInstruction].FunctionalUnitType;
	//get the number of RS alloted to this instruction
	int RS = activeInstructions[indexActiveInstruction].ReservationStation;
	if (RS == -1)
	{
		cout << "Error: A reservation station must be assigned before reaching the Write stage" << endl;
		return false;
	}
	//get the number of FU alloted to this instruction
	int FU = activeInstructions[indexActiveInstruction].FunctionalUnit;
	if (FU == -1)
	{
		cout << "Error: A Function Unit must be assigned before reaching the Write stage" << endl;
		return false;
	}
	//get the destination value for the current instruction
	//this is the value that the current instruction has produced
	int destFUType = ReservationStations[typeFU][RS].destination[0];
	int destRS = ReservationStations[typeFU][RS].destination[1];

	//check if current RS is there in register Result Status
	std::map< int, std::array<int, 2> >::iterator it = registerResultStatus.begin();
	while ( it != registerResultStatus.end() )
	{
		if (it->second[0] == destFUType && it->second[1] == destRS)
		{
			registerResultStatus.erase(it);
			it = registerResultStatus.begin();
		}
		else
		{
			++it;
		}
	}
	// release the functional unit
	FunctionalUnits[typeFU][FU].busy = false;
	// release the reservation station
	ReservationStations[typeFU][RS].busy = false;
	//remove the current instruction from the active instruction list
	activeInstructions.erase(activeInstructions.begin() + indexActiveInstruction);
	return true;
}

bool StallPipeline(int indexActiveInstruction, int CC)
{
	//current instruction is activeInstructions[indexActiveInstruction]
	if (activeInstructions.size() <= indexActiveInstruction)
	{
		cout << "Error: Error while calling StalPipeline()" << endl;
		return false;
	}	
	//check which type of Functional Unit is required by this instruction
	int typeFU = activeInstructions[indexActiveInstruction].FunctionalUnitType;
	bool flag = false;
	int RS;
	int WC = activeInstructions[indexActiveInstruction].WaitCode;
	switch (WC)
	{
	case StructuralHazard:
		flag = IssueInstruction(indexActiveInstruction);
		return flag;
	case WaitingForOperand:
		//get the number of RS alloted to this instruction
		RS = activeInstructions[indexActiveInstruction].ReservationStation;
		if (ReservationStations[typeFU][RS].source1Ready == true && ReservationStations[typeFU][RS].source2Ready == true)
		{
			//if both the operands are ready, then we can start executing the instruction in this clock cycle.
			activeInstructions[indexActiveInstruction].PipelineStage = Execute;
			//flag = ExecuteInstruction(indexActiveInstruction, CC);
			return true;
		}
		else
		{
			// operands are not ready yet, so we keep waiting
			return true;
		}
	case WaitingForFunctionalUnit:
		flag = ExecuteInstruction(indexActiveInstruction, CC);
		return flag;
	default:
		cout << "Error in StalPipeline(), unknown Wait Code" << endl;
		return false;
	}
}

//The function to execute the program. It will call required pipeline stage and will manage all the instructions.
bool executeProgram()
{	
	int CC = 1;
	while (true)
	{
		// Issue one new instruction in this CC
		int count = inputInstructions.size();
		if (count > 0)
		{
			// there is atleast one in-active instruction
			//create a new instruction
			instruction newInstruction;
			newInstruction.PipelineStage = Issue;
			newInstruction.inst = inputInstructions[0];
			newInstruction.FunctionalUnitType = inputInstructions[0][0];

			//append new instruction at the end of the active instruction queue
			activeInstructions.push_back(newInstruction);

			//remove the instruction from inputInstructions
			inputInstructions.erase(inputInstructions.begin());
		}

		//Perioritize the instructions curently in Write stage over anything else.
		//We check the entire activeInstruction queue and execute those instructions in order which are in Write stage
		vector<int> tempIndex;
		count = activeInstructions.size();
		for ( int i = 0; i < count; i++ )
		{
			if (activeInstructions[i].PipelineStage == Write)
			{
				bool flag = WriteBackStage1(i); //broadcast the newly calculated values
				if (flag == false)
				{
					cout << "Some problem in executing curent instruction. Aborting the execution.";
					return false;
				}
				tempIndex.push_back(i);
			}
		}
		
		// Execute one CC for all the active instructions
		count = activeInstructions.size();
		for (int i = 0; i < count; i++)
		{
			int currentPipelineStage = activeInstructions[i].PipelineStage;
			bool flag = false;
			switch (currentPipelineStage)
			{
			case Issue:
				flag = IssueInstruction(i);
				break;
			case Read:
				flag = ReadOperands(i);
				break;
			case Execute:
				flag = ExecuteInstruction(i, CC);
				break;
			case Write:
				flag = true;
				break;
			case Wait:
				flag = StallPipeline(i, CC);
				break;
			default:
				cout << "Unknown PipeLine Stage" << endl;
				break;
			}
			if (flag == false)
			{
				cout << "Problem in Current Clock Cycle" << endl;
				return false;
			}
		}

		//Release the resources hold by the instruction in write stage at start of this CC
		count = tempIndex.size();
		for (int i = count - 1; i >= 0 ; i--)
		{			
			bool flag = WriteBackStage2(tempIndex[i]);
			if (flag == false)
			{
				cout << "Some problem in executing curent instruction. Aborting the execution.";
				return false;
			}			
		}

		//each loop is one Clock Cycle
		//We stop when there is no active instruction
		if (activeInstructions.size() == 0)
		{
			break;
		}
		//Else we continue with a new clock cycle
		CC += 1;

		//for debugging
		printInputInstructions();
		printReservationStations();
		printRegisterStatus();
		printFunctionalUnits();
	}

	totalNumberOfClockCycles = CC;
	return true;
}

//Write the output file
void WriteOutputFile(std::string fileName)
{
	//Write the output file
	ofstream outputStatFile;
	outputStatFile.open(fileName);

	//write register content
	outputStatFile << "{\"cycles\":" << totalNumberOfClockCycles << " ," << endl;

	outputStatFile << "\"integer\" : [";
	int count = FunctionalUnits[IntegerIndex].size();
	for (int i = 0; i < count; i++)
	{
		outputStatFile << "{ \"id\" : " << i << " , \"instructions\" : " << FunctionalUnits[IntegerIndex][i].numberOfInstructionsExecuted << " }";
		if (i != count - 1)
		{
			outputStatFile << ", ";
		}
	}
	outputStatFile << "]," << endl;

	outputStatFile << "\"multiplier\" : [";
	count = FunctionalUnits[MultiplierIndex].size();
	for (int i = 0; i < count; i++)
	{
		outputStatFile << "{ \"id\" : " << i << " , \"instructions\" : " << FunctionalUnits[MultiplierIndex][i].numberOfInstructionsExecuted << " }";
		if (i != count - 1)
		{
			outputStatFile << ", ";
		}
	}
	outputStatFile << "]," << endl;

	outputStatFile << "\"divider\" : [";
	count = FunctionalUnits[DividerIndex].size();
	for (int i = 0; i < count; i++)
	{
		outputStatFile << "{ \"id\" : " << i << " , \"instructions\" : " << FunctionalUnits[DividerIndex][i].numberOfInstructionsExecuted << " }";
		if (i != count - 1)
		{
			outputStatFile << ", ";
		}
	}
	outputStatFile << "]," << endl;

	outputStatFile << "\"load\" : [";
	count = FunctionalUnits[LoadIndex].size();
	for (int i = 0; i < count; i++)
	{
		outputStatFile << "{ \"id\" : " << i << " , \"instructions\" : " << FunctionalUnits[LoadIndex][i].numberOfInstructionsExecuted << " }";
		if (i != count - 1)
		{
			outputStatFile << ", ";
		}
	}
	outputStatFile << "]," << endl;

	outputStatFile << "\"store\" : [";
	count = FunctionalUnits[StoreIndex].size();
	for (int i = 0; i < count; i++)
	{
		outputStatFile << "{ \"id\" : " << i << " , \"instructions\" : " << FunctionalUnits[StoreIndex][i].numberOfInstructionsExecuted << " }";
		if (i != count - 1)
		{
			outputStatFile << ", ";
		}
	}
	outputStatFile << "]," << endl;

	outputStatFile << "\"reg reads\" : " << numberOfOperandReadFromRegisterFile << " ," << endl;
	outputStatFile << "\"stalls\" : " << numberOfStructuralHazardStalls << "}" << endl;
	outputStatFile.close();
}


int main(int argc, char* argv[])
{
	if (argc != 4)
	{
		cout << "Usage: " << argv[0] << " <traceFile> <configFile> <outputfile> ";
		return 0;
	}
	initializeSimulator();

	//Read the Config File
	bool configFile = readConfigFile(argv[2]);
	if (configFile == false)
	{
		return 0;
	}
	//Read the trace File
	bool traceFile = readTraceFile(argv[1]);
	if (traceFile == false)
	{
		return 0;
	}
	printInputInstructions();
	printReservationStations();
	printFunctionalUnits();
	bool flag = executeProgram();
	if (flag == false)
	{
		return 0;
	}
	WriteOutputFile(argv[3]);
}


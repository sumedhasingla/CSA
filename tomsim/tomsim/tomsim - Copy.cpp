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

//structure
struct reservationStation {
	string operation;
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
	string type;
	bool busy = false;
	int reservationStationNumber;
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
std::map<int, int[2]> registerResultStatus;

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
				else if (opcode == 8)
				{
					currentInstruction.push_back( higherOrderBits & 7 );  // bit[10-8] destinationRegister
					currentInstruction.push_back( lowerOrderBits >> 5 ); //bit[7-5] sourceRegister
				}
				else if (opcode == 9)
				{
					currentInstruction.push_back( (lowerOrderBits >> 2) & 7 ); //bit[4-2] targetRegister
					currentInstruction.push_back( lowerOrderBits >> 5 ); //bit[7-5] sourceRegister
				}
				else if (opcode >= 16 && opcode <= 18)
				{
					currentInstruction.push_back( higherOrderBits & 7 );  // bit[10-8] destinationRegister
				}
				else if (opcode == 14)
				{
					currentInstruction.push_back( lowerOrderBits >> 5 ); //bit[7-5] sourceRegister
				}
				else if (opcode == 13)
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

void executeProgram()
{

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
			cout << "Reservation Station Number: " << j + 1<< endl;
			cout << "Status: " << ReservationStations[i][j].busy << endl;
			if (ReservationStations[i][j].source1Ready)
			{
				cout << "Source1 is ready with value: " << ReservationStations[i][j].source1Value << endl;
			}
			else
			{
				cout << "Source1 is waiting for Functional Unit Index: " << ReservationStations[i][j].source1Producer[0] << 
					" and Reservation Station: " << ReservationStations[i][j].source1Producer[1] <<endl;
			}
			if (ReservationStations[i][j].source2Ready)
			{
				cout << "Source2 is ready with value: " << ReservationStations[i][j].source2Value << endl;
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
	executeProgram();
}


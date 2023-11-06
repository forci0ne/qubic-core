#pragma once

////////// Scoring algorithm \\\\\\\\\\

#define SOLUTION_BUFFER_COUNT 8
static int miningData[DATA_LENGTH];
static struct
{
    int input[DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH];
    int output[INFO_LENGTH + NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH];
} neurons[SOLUTION_BUFFER_COUNT];
static struct
{
    char input[(NUMBER_OF_INPUT_NEURONS + INFO_LENGTH) * (DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH)];
    char output[(NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH) * (INFO_LENGTH + NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH)];
    unsigned short lengths[MAX_INPUT_DURATION * (NUMBER_OF_INPUT_NEURONS + INFO_LENGTH) + MAX_OUTPUT_DURATION * (NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH)];
} synapses[SOLUTION_BUFFER_COUNT];

struct
{
    char input[(NUMBER_OF_INPUT_NEURONS + INFO_LENGTH) * (DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH) / 4];
    char output[(NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH) * (INFO_LENGTH + NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH) / 4];
} synapses2Bit[SOLUTION_BUFFER_COUNT];

static volatile char solutionEngineLock[SOLUTION_BUFFER_COUNT];
static volatile char scoreCacheLock;

#if USE_SCORE_CACHE
struct
{
    unsigned char publicKey[32];
    unsigned char nonce[32];
    int score;
} scoreCache[SCORE_CACHE_SIZE]; // set zero or load from a file on init
static unsigned int scoreCacheHit = 0;
static unsigned int scoreCacheMiss = 0;
static unsigned int scoreCacheUnknown = 0;
void KangarooTwelve64To32(unsigned char* input, unsigned char* output);
static unsigned int getScoreCacheIndex(unsigned char* publicKey, unsigned char* nonce)
{
    unsigned char buffer[64];
    unsigned char digest[32];
    bs->CopyMem(buffer, publicKey, 32);
    bs->CopyMem(buffer+32, nonce, 32);
    KangarooTwelve64To32(buffer, digest);
    unsigned int result = *((unsigned long long*)digest) % SCORE_CACHE_SIZE;

    return result;
}

static int tryFetchingScoreCache(unsigned char* publicKey, unsigned char* nonce, unsigned int scoreCacheIndex)
{
    unsigned char* cachedPublicKey = scoreCache[scoreCacheIndex].publicKey;
    unsigned char* cachedNonce = scoreCache[scoreCacheIndex].nonce;
    if (EQUAL(*((__m256i*)cachedPublicKey), _mm256_setzero_si256()))
    {
        scoreCacheUnknown++;
        return -1;
    }
    if (EQUAL(*((__m256i*)cachedPublicKey), *((__m256i*)publicKey)) && EQUAL(*((__m256i*)cachedNonce), *((__m256i*)nonce)))
    {
        scoreCacheHit++;
        return scoreCache[scoreCacheIndex].score;
    }
    scoreCacheMiss++;
    return -1;
}

static void addScoreCache(unsigned char* publicKey, unsigned char* nonce, unsigned int scoreCacheIndex, int score)
{
    ACQUIRE(scoreCacheLock);
    bs->CopyMem(scoreCache[scoreCacheIndex].publicKey, publicKey, 32);
    bs->CopyMem(scoreCache[scoreCacheIndex].nonce, nonce, 32);
    scoreCache[scoreCacheIndex].score = score;
    RELEASE(scoreCacheLock);
}
#endif

static void setSynapse2Bits(char* p_nr, unsigned int idx, const char val) {
    int pos = ((idx & 3) << 1);
    *((unsigned char*)(&p_nr[idx >> 2])) = (*((unsigned char*)(&p_nr[idx >> 2])) & ~(3 << pos)) | ((val & 3) << pos);
}

static void setZeroSynapse2Bits(char* p_nr, unsigned int idx) {
    *((unsigned char*)(&p_nr[idx >> 2])) &= ~(3 << ((idx & 3) << 1));
}

static char getSynapse2Bits(char* p_nr, unsigned int idx) {
    char r = (((char*)p_nr)[idx >> 2] >> ((idx & 3) << 1)) & 3;
    return -2 * (r >> 1) + (r & 1);
}

static int computeNeuron64Bit(unsigned long long A, unsigned long long B) {
    unsigned long long A_high = A & 0xaaaaaaaaaaaaaaaall; // b10101010 10101010 high part of A
    unsigned long long A_low = A & 0x5555555555555555ll; // b01010101 01010101 low part of A
    unsigned long long B_high = B & 0xaaaaaaaaaaaaaaaall;
    unsigned long long B_low = B & 0x5555555555555555ll;
    unsigned long long zero_check = ((A_low << 1) | A_high);
    int lv = -2 * __popcnt64(A_high ^ B_high & zero_check);
    lv += __popcnt64(A_low & B_low);
    return lv;
}

static unsigned int score(const unsigned long long realProcessorNumber, unsigned char* publicKey, unsigned char* nonce)
{
    int score = 0;
#if USE_SCORE_CACHE
    unsigned int scoreCacheIndex = getScoreCacheIndex(publicKey, nonce);
    score = tryFetchingScoreCache(publicKey, nonce, scoreCacheIndex);
    if (score != -1)
    {
        return score;
    }
    score = 0;
#endif

    unsigned long long processorNumber = realProcessorNumber % 8;
    ACQUIRE(solutionEngineLock[processorNumber]);
    char neuronValue[1000];
    bs->SetMem(neuronValue, sizeof(neuronValue), 0);
    bs->SetMem(synapses2Bit[processorNumber].input, sizeof(synapses2Bit[processorNumber]), 0);
    random(publicKey, nonce, (unsigned char*)&synapses[processorNumber], sizeof(synapses[0]));
    for (unsigned int inputNeuronIndex = 0; inputNeuronIndex < NUMBER_OF_INPUT_NEURONS + INFO_LENGTH; inputNeuronIndex++)
    {
        for (unsigned int anotherInputNeuronIndex = 0; anotherInputNeuronIndex < DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH; anotherInputNeuronIndex++)
        {
            const unsigned int offset = inputNeuronIndex * (DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH) + anotherInputNeuronIndex;
            setSynapse2Bits((char*)(synapses2Bit[processorNumber].input), offset, char(((unsigned char)synapses[processorNumber].input[offset]) % 3) - 1);
        }
    }
    for (unsigned int outputNeuronIndex = 0; outputNeuronIndex < NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH; outputNeuronIndex++)
    {
        for (unsigned int anotherOutputNeuronIndex = 0; anotherOutputNeuronIndex < INFO_LENGTH + NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH; anotherOutputNeuronIndex++)
        {
            const unsigned int offset = outputNeuronIndex * (INFO_LENGTH + NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH) + anotherOutputNeuronIndex;
            setSynapse2Bits((char*)(synapses2Bit[processorNumber].output), offset, char(((unsigned char)synapses[processorNumber].output[offset]) % 3) - 1);
        }
    }
    for (unsigned int inputNeuronIndex = 0; inputNeuronIndex < NUMBER_OF_INPUT_NEURONS + INFO_LENGTH; inputNeuronIndex++)
    {
        setZeroSynapse2Bits((char*)(synapses2Bit[processorNumber].input), inputNeuronIndex * (DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH) + (DATA_LENGTH + inputNeuronIndex));
    }
    for (unsigned int outputNeuronIndex = 0; outputNeuronIndex < NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH; outputNeuronIndex++)
    {
        setZeroSynapse2Bits((char*)(synapses2Bit[processorNumber].output), outputNeuronIndex * (INFO_LENGTH + NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH) + (INFO_LENGTH + outputNeuronIndex));
    }

    unsigned int lengthIndex = 0;

    bs->CopyMem(&neurons[processorNumber].input[0], miningData, sizeof(miningData));
    bs->SetMem(&neurons[processorNumber].input[sizeof(miningData) / sizeof(neurons[0].input[0])], sizeof(neurons[0]) - sizeof(miningData), 0);

    for (int i = 0; i < DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH; i++) {
        setSynapse2Bits(neuronValue, i, neurons[processorNumber].input[i] >= 0 ? 1 : -1);
    }

    for (unsigned int tick = 0; tick < MAX_INPUT_DURATION; tick++)
    {
        unsigned short neuronIndices[NUMBER_OF_INPUT_NEURONS + INFO_LENGTH];
        unsigned short numberOfRemainingNeurons = 0;
        for (numberOfRemainingNeurons = 0; numberOfRemainingNeurons < NUMBER_OF_INPUT_NEURONS + INFO_LENGTH; numberOfRemainingNeurons++)
        {
            neuronIndices[numberOfRemainingNeurons] = numberOfRemainingNeurons;
        }
        while (numberOfRemainingNeurons)
        {
            const unsigned short neuronIndexIndex = synapses[processorNumber].lengths[lengthIndex++] % numberOfRemainingNeurons;
            const unsigned short inputNeuronIndex = neuronIndices[neuronIndexIndex];
            neuronIndices[neuronIndexIndex] = neuronIndices[--numberOfRemainingNeurons];

            // hot position
            int left = (DATA_LENGTH + inputNeuronIndex) / 32;
            unsigned long long* sy_ptr = (unsigned long long*)(synapses2Bit[processorNumber].input + (inputNeuronIndex * (DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH)) / 4);
            for (int i = 0; i < left; i++) {
                unsigned long long A = sy_ptr[i];
                unsigned long long B = ((unsigned long long*)neuronValue)[i];
                int lv = computeNeuron64Bit(A, B);
                neurons[processorNumber].input[DATA_LENGTH + inputNeuronIndex] += lv;
            }
            for (int i = left * 32; i < (left + 1) * 32; i++) {
                int value = neurons[processorNumber].input[i] >= 0 ? 1 : -1;
                char sy = getSynapse2Bits((char*)synapses2Bit[processorNumber].input, inputNeuronIndex * (DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH) + i);
                value *= sy;
                neurons[processorNumber].input[DATA_LENGTH + inputNeuronIndex] += value;
            }
            // update hot position
            for (int i = (left + 1); i < (DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH) / 32; i++) {
                unsigned long long A = sy_ptr[i];
                unsigned long long B = ((unsigned long long*)neuronValue)[i];
                int lv = computeNeuron64Bit(A, B);
                neurons[processorNumber].input[DATA_LENGTH + inputNeuronIndex] += lv;
            }
            setSynapse2Bits(neuronValue, DATA_LENGTH + inputNeuronIndex, neurons[processorNumber].input[DATA_LENGTH + inputNeuronIndex] >= 0 ? 1 : -1);
        }
    }

    bs->CopyMem(&neurons[processorNumber].output[0], &neurons[processorNumber].input[DATA_LENGTH + NUMBER_OF_INPUT_NEURONS], INFO_LENGTH * sizeof(neurons[0].input[0]));
    for (int i = 0; i < DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH; i++) {
        setSynapse2Bits(neuronValue, i, neurons[processorNumber].output[i] >= 0 ? 1 : -1);
    }
    for (unsigned int tick = 0; tick < MAX_OUTPUT_DURATION; tick++)
    {
        unsigned short neuronIndices[NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH];
        unsigned short numberOfRemainingNeurons = 0;
        for (numberOfRemainingNeurons = 0; numberOfRemainingNeurons < NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH; numberOfRemainingNeurons++)
        {
            neuronIndices[numberOfRemainingNeurons] = numberOfRemainingNeurons;
        }
        while (numberOfRemainingNeurons)
        {
            const unsigned short neuronIndexIndex = synapses[processorNumber].lengths[lengthIndex++] % numberOfRemainingNeurons;
            const unsigned short outputNeuronIndex = neuronIndices[neuronIndexIndex];
            neuronIndices[neuronIndexIndex] = neuronIndices[--numberOfRemainingNeurons];
            // hot position
            int left = (INFO_LENGTH + outputNeuronIndex) / 32;
            unsigned long long* sy_ptr = (unsigned long long*)(synapses2Bit[processorNumber].output + (outputNeuronIndex * (DATA_LENGTH + NUMBER_OF_OUTPUT_NEURONS + INFO_LENGTH)) / 4);
            {
                int i = 0;
                while (i < left)
                {
                    unsigned long long A = sy_ptr[i];
                    unsigned long long B = ((unsigned long long*)neuronValue)[i];
                    int lv = computeNeuron64Bit(A, B);
                    neurons[processorNumber].output[INFO_LENGTH + outputNeuronIndex] += lv;
                    i++;
                }
            }

            for (int i = left * 32; i < (left + 1) * 32; i++) {
                int value = neurons[processorNumber].output[i] >= 0 ? 1 : -1;
                char sy = getSynapse2Bits((char*)synapses2Bit[processorNumber].output, outputNeuronIndex * (DATA_LENGTH + NUMBER_OF_OUTPUT_NEURONS + INFO_LENGTH) + i);
                value *= sy;
                neurons[processorNumber].output[INFO_LENGTH + outputNeuronIndex] += value;
            }
            // update hot position
            for (int i = (left + 1); i < (DATA_LENGTH + NUMBER_OF_OUTPUT_NEURONS + INFO_LENGTH) / 32; i++) {
                unsigned long long A = sy_ptr[i];
                unsigned long long B = ((unsigned long long*)neuronValue)[i];
                int lv = computeNeuron64Bit(A, B);
                neurons[processorNumber].output[INFO_LENGTH + outputNeuronIndex] += lv;
            }
            setSynapse2Bits(neuronValue, INFO_LENGTH + outputNeuronIndex, neurons[processorNumber].output[INFO_LENGTH + outputNeuronIndex] >= 0 ? 1 : -1);
        }
    }

    for (unsigned int i = 0; i < DATA_LENGTH; i++)
    {
        if ((miningData[i] >= 0) == (neurons[processorNumber].output[INFO_LENGTH + NUMBER_OF_OUTPUT_NEURONS + i] >= 0))
        {
            score++;
        }
    }

    RELEASE(solutionEngineLock[processorNumber]);
#if USE_SCORE_CACHE
    addScoreCache(publicKey, nonce, scoreCacheIndex, score);
#endif
    return score;
}
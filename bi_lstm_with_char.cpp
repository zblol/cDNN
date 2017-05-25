//
// Created by Boliang Zhang on 5/19/17.
//
#include <iostream>
#include <numeric>
#include <ctime>
#include "nn.h"
#include "bi_lstm_with_char.h"
#include "loader.h"

//
// network configuration
//
int wordDim = 50;
int charDim = 25;
int wordLSTMHiddenDim = 100;
int charLSTMHiddenDim = 25;
double learningRate = 0.01;
double dropoutRate = 0.5;

//
// define network parameter, cache and diff
//
LSTMParameters charFWDLSTMParam;
LSTMParameters charBWDLSTMParam;
LSTMParameters wordFWDLSTMParam;
LSTMParameters wordBWDLSTMParam;
MLPParameters mlpParameters;

LSTMCache charFWDLSTMCache;
std::vector<LSTMCache> charFWDLSTMCacheVec;
LSTMCache charBWDLSTMCache;
std::vector<LSTMCache> charBWDLSTMCacheVec;
LSTMCache wordFWDLSTMCache;
LSTMCache wordBWDLSTMCache;
MLPCache mlpCache;
DropoutCache dropoutCache;
CrossEntropyCache crossEntropyCache;

LSTMDiff charFWDLSTMDiff;
LSTMDiff charBWDLSTMDiff;
LSTMDiff wordFWDLSTMDiff;
LSTMDiff wordBWDLSTMDiff;
MLPDiff mlpDiff;
DropoutDiff dropoutDiff;
CrossEntropyDiff crossEntropyDiff;


void networkForward(const Sequence & s,
                    Eigen::MatrixXd & loss,
                    Eigen::MatrixXd & pred,
                    bool isTrain){
    int sequenceLen = s.charEmb.size();

    // bi-directional char lstm forward
    Eigen::MatrixXd sequcenCharEmb(2 * charLSTMHiddenDim, sequenceLen);
    charFWDLSTMCacheVec.clear();
    charBWDLSTMCacheVec.clear();
    for (int j = 0; j < sequenceLen; ++j ) {
        lstmForward(s.charEmb[j], charFWDLSTMParam, charFWDLSTMCache);
        LSTMCache cFWD = charFWDLSTMCache;
        charFWDLSTMCacheVec.push_back(cFWD);
        lstmForward(s.charEmb[j].colwise().reverse(), charBWDLSTMParam, charBWDLSTMCache);
        LSTMCache cBWD = charBWDLSTMCache;
        charBWDLSTMCacheVec.push_back(cBWD);
        sequcenCharEmb.col(j) << charFWDLSTMCache.h.rightCols(1), charBWDLSTMCache.h.rightCols(1);
    }
    // input dropout forward
    Eigen::MatrixXd dropoutInput(wordDim + 2 * charLSTMHiddenDim, sequenceLen);
    dropoutInput << s.wordEmb, sequcenCharEmb;  // concatenate word embedding and two character embeddings.
    dropoutForward(dropoutInput, dropoutRate, dropoutCache);

    // bi-directional word lstm forward
    lstmForward(dropoutCache.y, wordFWDLSTMParam, wordFWDLSTMCache);
    lstmForward(dropoutCache.y.colwise().reverse(), wordBWDLSTMParam, wordBWDLSTMCache);

    // mlp forward
    Eigen::MatrixXd mlpInput(2 * wordLSTMHiddenDim, sequenceLen);
    mlpInput << wordFWDLSTMCache.h, wordBWDLSTMCache.h.colwise().reverse();
    mlpForward(mlpInput, mlpParameters, mlpCache);

    // cross entropy forward
    crossEntropyForward(mlpCache.y, s.labelOneHot, crossEntropyCache);

    loss = crossEntropyCache.loss;
    pred = crossEntropyCache.pred;
}

void networkBackward(const Sequence & s){
    int sequenceLen = s.charEmb.size();

    // cross entropy backward
    crossEntropyBackward(crossEntropyCache, crossEntropyDiff);

    // mlp backward
    mlpBackward(crossEntropyDiff.pred_diff, mlpParameters, mlpCache, mlpDiff);

    // word lstm backward
    Eigen::MatrixXd wordFWDLSTMdy = mlpDiff.x_diff.topRows(wordLSTMHiddenDim);
    lstmBackward(wordFWDLSTMdy, wordFWDLSTMParam, wordFWDLSTMCache, wordFWDLSTMDiff);

    Eigen::MatrixXd wordBWDLSTMdy = mlpDiff.x_diff.bottomRows(wordLSTMHiddenDim).colwise().reverse();
    lstmBackward(wordBWDLSTMdy, wordBWDLSTMParam, wordBWDLSTMCache, wordBWDLSTMDiff);

    // dropout backward
    Eigen::MatrixXd droputDy = wordFWDLSTMDiff.x_diff + wordBWDLSTMDiff.x_diff.colwise().reverse();
    dropoutBackward(droputDy, dropoutCache, dropoutDiff);

    // char lstm backward
    Eigen::MatrixXd charFWDLSTMdy = dropoutDiff.x_diff.block(wordDim, 0, charLSTMHiddenDim, sequenceLen);
    Eigen::MatrixXd charBWDLSTMdy = dropoutDiff.x_diff.block(wordDim + charLSTMHiddenDim, 0, charLSTMHiddenDim, sequenceLen);

    for (int i = 0; i < sequenceLen; i++) {
        int tokenLen = s.charEmb[i].cols();
        Eigen::MatrixXd tmpDy(charLSTMHiddenDim, tokenLen);
        tmpDy.setZero();

        tmpDy.rightCols(1) = charFWDLSTMdy.col(i);

        LSTMDiff tmpCharFWDLSTMDiff;
        lstmBackward(tmpDy, charFWDLSTMParam, charFWDLSTMCacheVec[i], tmpCharFWDLSTMDiff);
        if (i == 0) {
            charFWDLSTMDiff = tmpCharFWDLSTMDiff;
        } else {
            charFWDLSTMDiff += tmpCharFWDLSTMDiff;
        }

        tmpDy.rightCols(1) = charBWDLSTMdy.col(i);

        LSTMDiff tmpCharBWDLSTMDiff;
        lstmBackward(tmpDy, charBWDLSTMParam, charBWDLSTMCacheVec[i], tmpCharBWDLSTMDiff);
        if (i == 0) {
            charBWDLSTMDiff = tmpCharBWDLSTMDiff;
        } else {
            charBWDLSTMDiff += tmpCharBWDLSTMDiff;
        }

    }
}

void networkParamUpdate(Sequence & s,
                        Eigen::MatrixXd & wordEmbedding) {
    mlpParamUpdate(learningRate, mlpParameters, mlpDiff);
    lstmParamUpdate(learningRate, wordFWDLSTMParam, wordFWDLSTMDiff);
    lstmParamUpdate(learningRate, wordBWDLSTMParam, wordBWDLSTMDiff);
    lstmParamUpdate(learningRate, charFWDLSTMParam, charFWDLSTMDiff);
    lstmParamUpdate(learningRate, charBWDLSTMParam, charBWDLSTMDiff);
    dropoutInputUpdate(learningRate, s, wordEmbedding, dropoutDiff);
}

void paramGradCheck(const Sequence s,
                    Eigen::MatrixXd & paramToCheck,
                    const Eigen::MatrixXd & paramGrad
){
    dropoutRate = -1;
    std::cout.precision(15);

    int num_checks = 10;
    double delta = 10e-5;

    assert(paramToCheck.rows() == paramGrad.rows() || paramToCheck.cols() == paramGrad.cols());

    for (int i = 0; i < num_checks; ++i) {
        int randRow = 0 + (rand() % (int)(paramToCheck.rows()));
        int randCol = 0 + (rand() % (int)(paramToCheck.cols()));

        double originalVal = paramToCheck(randRow, randCol);

        Eigen::MatrixXd _;

        Eigen::MatrixXd loss0;
        paramToCheck(randRow, randCol) = originalVal - delta;
        networkForward(s, loss0, _, true);

        Eigen::MatrixXd loss1;
        paramToCheck(randRow, randCol) = originalVal + delta;
        networkForward(s, loss1, _, true);

        paramToCheck(randRow, randCol) = originalVal;

        double analyticGrad = paramGrad(randRow, randCol);
        double numericalGrad = (loss1 - loss0).sum() / (2.0 * delta);
        double rel_error = fabs(analyticGrad - numericalGrad) / fabs(analyticGrad + numericalGrad);

        std::cout << "\t" << numericalGrad << ", " << analyticGrad << " ==> " << rel_error << std::endl;
    }
    dropoutRate = 0.5;
}

void inputGradCheck(const Sequence & s){
    Sequence sCopy = s;

    Eigen::MatrixXd dWordEmb = dropoutDiff.x_diff.topRows(wordDim);
    std::cout.precision(15);
    dropoutRate = -1;

    int num_checks = 10;
    double delta = 10e-5;

    for (int i = 0; i < num_checks; ++i) {
        int randRow = 0 + (rand() % (int)(sCopy.wordEmb.rows()));
        int randCol = 0 + (rand() % (int)(sCopy.wordEmb.cols()));

        double originalVal = sCopy.wordEmb(randRow, randCol);

        Eigen::MatrixXd _;

        Eigen::MatrixXd loss0;
        sCopy.wordEmb(randRow, randCol) = originalVal - delta;
        networkForward(sCopy, loss0, _, true);

        Eigen::MatrixXd loss1;
        sCopy.wordEmb(randRow, randCol) = originalVal + delta;
        networkForward(sCopy, loss1, _, true);

        sCopy.wordEmb(randRow, randCol) = originalVal;

        double analyticGrad = dWordEmb(randRow, randCol);
        double numericalGrad = (loss1 - loss0).sum() / (2 * delta);
        double rel_error = fabs(analyticGrad - numericalGrad) / fabs(analyticGrad + numericalGrad);

        std::cout << "\t" << numericalGrad << ", " << analyticGrad << " ==> " << rel_error << std::endl;
    }
    dropoutRate = 0.5;
}

void networkGradientCheck(const Sequence & s){
    std::cout << "####### checking mlpParameters ########" << std::endl;
    std::cout << "=> gradient checking W" << std::endl;
    paramGradCheck(s, mlpParameters.W, mlpDiff.W_diff);
    std::cout << "=> gradient checking b" << std::endl;
    paramGradCheck(s, mlpParameters.W, mlpDiff.W_diff);

    std::cout << "####### checking wordFWDLSTMDiff ########" << std::endl;
    std::cout << "=> gradient checking Wi" << std::endl;
    paramGradCheck(s, wordFWDLSTMParam.Wi, wordFWDLSTMDiff.Wi_diff);
    std::cout << "=> gradient checking Wf" << std::endl;
    paramGradCheck(s, wordFWDLSTMParam.Wf, wordFWDLSTMDiff.Wf_diff);
    std::cout << "=> gradient checking Wc" << std::endl;
    paramGradCheck(s, wordFWDLSTMParam.Wc, wordFWDLSTMDiff.Wc_diff);
    std::cout << "=> gradient checking Wo" << std::endl;
    paramGradCheck(s, wordFWDLSTMParam.Wo, wordFWDLSTMDiff.Wo_diff);
    std::cout << "=> gradient checking bi" << std::endl;
    paramGradCheck(s, wordFWDLSTMParam.bi, wordFWDLSTMDiff.bi_diff);
    std::cout << "=> gradient checking bf" << std::endl;
    paramGradCheck(s, wordFWDLSTMParam.bf, wordFWDLSTMDiff.bf_diff);
    std::cout << "=> gradient checking bc" << std::endl;
    paramGradCheck(s, wordFWDLSTMParam.bc, wordFWDLSTMDiff.bc_diff);
    std::cout << "=> gradient checking bo" << std::endl;
    paramGradCheck(s, wordFWDLSTMParam.bo, wordFWDLSTMDiff.bo_diff);

    std::cout << "####### checking wordBWDLSTMDiff ########" << std::endl;
    std::cout << "=> gradient checking Wi" << std::endl;
    paramGradCheck(s, wordBWDLSTMParam.Wi, wordBWDLSTMDiff.Wi_diff);
    std::cout << "=> gradient checking Wf" << std::endl;
    paramGradCheck(s, wordBWDLSTMParam.Wf, wordBWDLSTMDiff.Wf_diff);
    std::cout << "=> gradient checking Wc" << std::endl;
    paramGradCheck(s, wordBWDLSTMParam.Wc, wordBWDLSTMDiff.Wc_diff);
    std::cout << "=> gradient checking Wo" << std::endl;
    paramGradCheck(s, wordBWDLSTMParam.Wo, wordBWDLSTMDiff.Wo_diff);
    std::cout << "=> gradient checking bi" << std::endl;
    paramGradCheck(s, wordBWDLSTMParam.bi, wordBWDLSTMDiff.bi_diff);
    std::cout << "=> gradient checking bf" << std::endl;
    paramGradCheck(s, wordBWDLSTMParam.bf, wordBWDLSTMDiff.bf_diff);
    std::cout << "=> gradient checking bc" << std::endl;
    paramGradCheck(s, wordBWDLSTMParam.bc, wordBWDLSTMDiff.bc_diff);
    std::cout << "=> gradient checking bo" << std::endl;
    paramGradCheck(s, wordBWDLSTMParam.bo, wordBWDLSTMDiff.bo_diff);
    std::cout << "=> gradient checking x" << std::endl;

    std::cout << "####### checking charFWDLSTMDiff ########" << std::endl;
    std::cout << "=> gradient checking Wi" << std::endl;
    paramGradCheck(s, charFWDLSTMParam.Wi, charFWDLSTMDiff.Wi_diff);
    std::cout << "=> gradient checking Wf" << std::endl;
    paramGradCheck(s, charFWDLSTMParam.Wf, charFWDLSTMDiff.Wf_diff);
    std::cout << "=> gradient checking Wc" << std::endl;
    paramGradCheck(s, charFWDLSTMParam.Wc, charFWDLSTMDiff.Wc_diff);
    std::cout << "=> gradient checking Wo" << std::endl;
    paramGradCheck(s, charFWDLSTMParam.Wo, charFWDLSTMDiff.Wo_diff);
    std::cout << "=> gradient checking bi" << std::endl;
    paramGradCheck(s, charFWDLSTMParam.bi, charFWDLSTMDiff.bi_diff);
    std::cout << "=> gradient checking bf" << std::endl;
    paramGradCheck(s, charFWDLSTMParam.bf, charFWDLSTMDiff.bf_diff);
    std::cout << "=> gradient checking bc" << std::endl;
    paramGradCheck(s, charFWDLSTMParam.bc, charFWDLSTMDiff.bc_diff);
    std::cout << "=> gradient checking bo" << std::endl;
    paramGradCheck(s, charFWDLSTMParam.bo, charFWDLSTMDiff.bo_diff);

    std::cout << "####### checking charBWDLSTMDiff ########" << std::endl;
    std::cout << "=> gradient checking Wi" << std::endl;
    paramGradCheck(s, charBWDLSTMParam.Wi, charBWDLSTMDiff.Wi_diff);
    std::cout << "=> gradient checking Wf" << std::endl;
    paramGradCheck(s, charBWDLSTMParam.Wf, charBWDLSTMDiff.Wf_diff);
    std::cout << "=> gradient checking Wc" << std::endl;
    paramGradCheck(s, charBWDLSTMParam.Wc, charBWDLSTMDiff.Wc_diff);
    std::cout << "=> gradient checking Wo" << std::endl;
    paramGradCheck(s, charBWDLSTMParam.Wo, charBWDLSTMDiff.Wo_diff);
    std::cout << "=> gradient checking bi" << std::endl;
    paramGradCheck(s, charBWDLSTMParam.bi, charBWDLSTMDiff.bi_diff);
    std::cout << "=> gradient checking bf" << std::endl;
    paramGradCheck(s, charBWDLSTMParam.bf, charBWDLSTMDiff.bf_diff);
    std::cout << "=> gradient checking bc" << std::endl;
    paramGradCheck(s, charBWDLSTMParam.bc, charBWDLSTMDiff.bc_diff);
    std::cout << "=> gradient checking bo" << std::endl;
    paramGradCheck(s, charBWDLSTMParam.bo, charBWDLSTMDiff.bo_diff);

    std::cout << "=> gradient checking wordEmb" << std::endl;
    inputGradCheck(s);

}

void train(const std::vector<Sequence>& training,
           const std::vector<Sequence>& eval,
           Eigen::MatrixXd & wordEmbedding,
           const Eigen::MatrixXd & charEmbedding) {
    //
    // initialize parameters
    //
    lstmInit(charDim, charLSTMHiddenDim, charFWDLSTMParam);
    lstmInit(charDim, charLSTMHiddenDim, charBWDLSTMParam);
    lstmInit(wordDim + 2 * charLSTMHiddenDim, wordLSTMHiddenDim, wordFWDLSTMParam);
    lstmInit(wordDim + 2 * charLSTMHiddenDim, wordLSTMHiddenDim, wordBWDLSTMParam);
    int labelSize = training[0].labelOneHot.rows();
    mlpInit(2 * wordLSTMHiddenDim, labelSize, mlpParameters);

    int epoch = 100;
    float bestAcc = 0;
    for (int i = 0; i < epoch; i++) {
        time_t startTime = time(0);
        //
        // train
        //
        std::cout << "=> " << i << " epoch training starts..." << std::endl;

        int numSeqToReport = 1000;
        std::vector<float> epoch_loss;
        std::vector<int> index(training.size());
        std::iota(index.begin(), index.end(), 1);
        std::random_shuffle(index.begin(), index.end());
        for (int j = 0; j < training.size(); ++j){
            Sequence s = training[index[j]-1];

            processData(s, wordEmbedding, charEmbedding);
            //
            // network forward
            //
            Eigen::MatrixXd loss;
            Eigen::MatrixXd pred;

            networkForward(s, loss, pred, true);
            epoch_loss.push_back(loss.sum() / s.seqLen);

            //
            // network backward
            //
            networkBackward(s);

            //
            // network gradient check
            //
//            networkGradientCheck(s);

            //
            // network parameters update
            //
            networkParamUpdate(s, wordEmbedding);

            if ((j + 1) % numSeqToReport == 0){
                std::vector<float> v(epoch_loss.end() - numSeqToReport, epoch_loss.end());
                float average = std::accumulate( v.begin(), v.end(), 0.0)/v.size();
                std::cout << j + 1 << " sequences loss: " << average << std::endl;
            }
        }
        float average = std::accumulate( epoch_loss.begin(), epoch_loss.end(), 0.0)/epoch_loss.size();
        std::cout << "epoch loss: " << average << std::endl;
        time_t trainTime = time(0) - startTime;
        std::printf("time elapsed: %d seconds (%.4f sec/sentence)\n", int(trainTime), float(trainTime) / training.size());

        //
        // eval
        //
        int numTags = 0;
        int numCorrectTags = 0;
        for (int j = 0; j < eval.size(); ++j ) {
            Sequence s = eval[j];
            processData(s, wordEmbedding, charEmbedding);

            Eigen::MatrixXd loss;
            Eigen::MatrixXd pred;
            networkForward(s, loss, pred, false);

            std::vector<int> predLabelIndex;
            Eigen::MatrixXd maxProba = pred.colwise().maxCoeff().transpose();
            for (int k = 0; k < s.labelIndex.size(); ++k) {
                for (int l = 0; l < pred.rows(); ++l) {
                    if (pred(l, k) == maxProba(k, 0))
                        predLabelIndex.push_back(l);
                }
            }
            numTags += s.labelIndex.size();
            for (int k = 0; k < s.labelIndex.size(); ++k) {
                if (predLabelIndex[k] == s.labelIndex[k])
                    numCorrectTags += 1;
            }
        }
        float acc = float(numCorrectTags) / numTags * 100;
        if (acc > bestAcc) {
            std::printf("new best accuracy on eval set: %.2f%% (%d/%d)\n\n",
                        acc, numCorrectTags, numTags);
            bestAcc = acc;
        } else {
            std::printf("accuracy on eval set: %.2f%% (%d/%d)\n\n",
                        acc, numCorrectTags, numTags);
        }
    }
}

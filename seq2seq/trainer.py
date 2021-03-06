import argparse
import json
import collections
import os
import datetime
import sys
import utils
import shutil
import loader
import itertools
import pickle
import subprocess
import codecs


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('trainer_json')
    parser.add_argument('-c', action="store_true", default=False,
                        help="compile trainer")

    args = parser.parse_args()

    conf = json.load(open(args.trainer_json),
                     object_pairs_hook=collections.OrderedDict)

    # check train file
    if not os.path.exists(conf['train_file']):
        raise IOError("train file not found.")
    # check eval file
    if not os.path.exists(conf['eval_file']):
        raise IOError("eval file not found.")
    # check model dir
    if not os.path.exists(conf['model_dir']) or \
            not os.path.isdir(conf['model_dir']):
        raise IOError("model dir not found")

    # create a unique model id by datetime
    date = datetime.datetime.date(datetime.datetime.now())
    time = datetime.datetime.time(datetime.datetime.now())
    model_id = '_'.join([str(item) for item in
                         [date.year, date.month, date.day,
                          time.hour, time.minute, time.second,
                          time.microsecond]
                         ])
    conf['model_dir'] = os.path.join(conf['model_dir'], model_id)
    os.mkdir(conf['model_dir'])

    # create directory in model_dir to save eval results for each epoch
    os.mkdir(os.path.join(conf['model_dir'], "eval_scores"))
    # create directory in model_dir to save model parameters
    os.mkdir(os.path.join(conf['model_dir'], "model"))

    # register logger to save print (messages to both stdout and disk)
    training_log_path = os.path.join(conf['model_dir'], 'training_log.txt')
    if os.path.exists(training_log_path):
        os.remove(training_log_path)
    f = open(training_log_path, 'w')
    sys.stdout = utils.Tee(sys.stdout, f)

    # copy trainer config json to model dir
    print("=> net configuration")
    shutil.copy(args.trainer_json, conf['model_dir'])

    # print net configuration
    for key, value in conf.items():
        print("%s = %s" % (key, value))

    # load train and eval data
    train_src_tokens, train_trg_tokens = loader.load_news15(conf['train_file'])
    for i, item in enumerate(train_trg_tokens):          # add start and end
        train_trg_tokens[i] = ["<s>"] + item + ["</s>"]  # to target sentences

    eval_src_tokens, eval_trg_tokens = loader.load_news15(conf['eval_file'])

    # generate mapping
    src_tokens = set(itertools.chain(*(train_src_tokens + eval_src_tokens)))
    trg_tokens = set(itertools.chain(*(train_trg_tokens + eval_trg_tokens)))

    src_token2id, src_id2token = loader.generate_mapping(src_tokens,
                                                         is_trg=False)
    trg_token2id, trg_id2token = loader.generate_mapping(trg_tokens,
                                                         is_trg=True)

    # save mapping to file
    with codecs.open(
            os.path.join(conf['model_dir'], 'model', 'src_token2id.mdl'),
            'w',
            'utf-8') as f:
        for k, v in src_token2id.items():
            f.write('%s %d\n' % (k, v))

    with codecs.open(
            os.path.join(conf['model_dir'], 'model', 'src_id2token.mdl'),
            'w',
            'utf-8') as f:
        for k, v in src_id2token.items():
            f.write('%d %s\n' % (k, v))

    with codecs.open(
            os.path.join(conf['model_dir'], 'model', 'trg_token2id.mdl'),
            'w',
            'utf-8') as f:
        for k, v in trg_token2id.items():
            f.write('%s %d\n' % (k, v))

    with codecs.open(
            os.path.join(conf['model_dir'], 'model', 'trg_id2token.mdl'),
            'w',
            'utf-8') as f:
        for k, v in trg_id2token.items():
            f.write('%d %s\n' % (k, v))

    # save processed train and test set
    train_dat = os.path.join(conf['model_dir'], 'train.dat')
    with open(train_dat, 'w') as f:
        for src, trg in zip(train_src_tokens, train_trg_tokens):
            f.write('%s\t%s\n' % (' '.join(src), ' '.join(trg)))
    test_dat = os.path.join(conf['model_dir'], 'eval.dat')
    with open(test_dat, 'w') as f:
        for src, trg in zip(eval_src_tokens, eval_trg_tokens):
            f.write('%s\t%s\n' % (' '.join(src), ' '.join(trg)))

    # compile trainer
    root_dir = os.path.dirname(os.path.abspath("__file__"))
    trainer_exe = os.path.join(root_dir, "../bin/seq2SeqTrainer")
    eigen_path = os.path.join(root_dir, '../third_party/eigen/')
    cmd = ['g++', '-std=c++11', '-O3', '-pthread', '-I', eigen_path,
           os.path.join(root_dir, '../nn.cpp'),
           os.path.join(root_dir, '../utils.cpp'),
           os.path.join(root_dir, '../net.cpp'),
           os.path.join(root_dir, 'trainer.cpp'),
           os.path.join(root_dir, 'loader.cpp'),
           os.path.join(root_dir, 'seq2seq.cpp'),
           '-o', trainer_exe]
    print('=> compiling trainer...')
    print(' '.join(cmd))
    subprocess.call(' '.join(cmd), shell=True)

    # executing trainer
    cmd = [
        trainer_exe,
        train_dat,
        test_dat,
        conf['model_dir'],
        conf['token_dim'],
        conf['token_lstm_dim'],
        conf['learning_rate'],
        conf['encoder_stack'],
        conf['decoder_stack'],
        conf['dropout'],
        conf['num_thread'],
        conf['num_epoch']
    ]
    print('=> executing trainer...')
    print(' '.join(cmd))

    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    for line in p.stdout:
        print(str(line.rstrip(), 'utf-8'))
        p.stdout.flush()





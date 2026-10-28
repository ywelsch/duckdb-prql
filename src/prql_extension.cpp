#define DUCKDB_EXTENSION_MAIN

#include "prql_extension.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/extension_statement.hpp"

#include "prqlc.hpp"

#include "re2/re2.h"

#include <sstream>

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
  auto &instance = loader.GetDatabaseInstance();
  auto &config = DBConfig::GetConfig(instance);
  PrqlParserExtension prql_parser;
  ParserExtension::Register(config, prql_parser);
  OperatorExtension::Register(config, make_shared_ptr<PrqlOperatorExtension>());
}

void PrqlExtension::Load(ExtensionLoader &loader) { LoadInternal(loader); }

void transform_block(const std::string &block, std::stringstream &ss,
                     bool &failed) {
  prqlc::Options options;
  options.format = false;
  options.target = const_cast<char *>("sql.duckdb");
  options.signature_comment = false;
  prqlc::CompileResult compile_result = compile(block.c_str(), &options);

  for (int i = 0; i < compile_result.messages_len; i++) {
    prqlc::Message const *e = &compile_result.messages[i];
    if (e->kind == prqlc::MessageKind::Error) {
      if (failed) {
        // append new line for next failure
        ss << "\n";
      }
      if (e->display != NULL) {
        ss << *e->display;
      } else if (e->code != NULL) {
        ss << "[" << *e->code << "] Error: " << e->reason;
      } else {
        ss << "Error: " << e->reason;
      }
      failed = true;
    }
  }
  if (!failed) {
    ss << compile_result.output;
  }
  prqlc::result_destroy(compile_result);
}

// Compiles a PRQL query, or a SQL query with embedded PRQL blocks, to SQL.
// Returns false if compilation failed, in which case sql_query_or_error
// contains the error message.
static bool compile_prql(const std::string &query,
                         std::string &sql_query_or_error) {
  // remove trailing semicolon
  string trimmed_string = query;
  StringUtil::RTrim(trimmed_string);
  if (!trimmed_string.empty() && trimmed_string.back() == ';') {
    trimmed_string.pop_back();
  }

  // Identify PRQL blocks, delimited by "(|" and "|)"
  RE2::Options options;
  options.set_dot_nl(true);
  RE2 block_re("(.*?)[(][|](.*?)[|][)]", options);
  duckdb_re2::StringPiece input(trimmed_string);
  std::string pre_block_command;
  std::string block_command;

  bool failed = false;
  bool block_found = false;
  std::stringstream ss;

  while (RE2::Consume(&input, block_re, &pre_block_command, &block_command)) {
    block_found = true;
    ss << pre_block_command;
    ss << "(";
    transform_block(block_command, ss, failed);
    ss << ")";
  }
  std::string post_block_command;
  post_block_command = input.ToString();
  if (block_found) {
    ss << post_block_command;
  } else {
    transform_block(post_block_command, ss, failed);
  }
  sql_query_or_error = ss.str();
  return !failed;
}

ParserExtensionParseResult prql_parse(ParserExtensionInfo *,
                                      const vector<SimpleToken> &tokens) {
  // The tokens don't retain whitespace, which is significant in PRQL, so we
  // only claim the tokens of the statement here and compile the PRQL in
  // prql_bind instead, where the statement's source text is available.
  // TODO: decide when to consider it a PRQL statement vs this parser extension
  //  not being responsible for parsing the statement.
  //  For now, we consider any malformed statement a PRQL one.
  //  In the future we can try using a couple of basic heuristics to classify
  //  (e.g. only those queries starting with "from"),
  //  or we could mandate that PRQL queries in duckdb are always prefixed with
  //  a certain string / symbol, e.g. |>, and then remove that here.
  idx_t end = 0;
  while (end < tokens.size() && tokens[end].type != TokenType::TERMINATOR &&
         tokens[end].type != TokenType::END_OF_INPUT) {
    end++;
  }
  if (end == 0) {
    // empty statement
    return ParserExtensionParseResult();
  }
  ParserExtensionParseResult result(
      make_uniq_base<ParserExtensionParseData, PrqlParseData>());
  // also claim the terminator, just like a regular statement
  result.consumed_tokens =
      NumericCast<int64_t>(MinValue<idx_t>(end + 1, tokens.size()));
  return result;
}

ParserExtensionPlanResult
prql_plan(ParserExtensionInfo *, ClientContext &context,
          unique_ptr<ParserExtensionParseData> parse_data) {
  // Throwing an exception here allows the planning to be picked up by
  // prql_bind instead, which has access to the statement's source text.
  throw BinderException("Use prql_bind instead");
}

BoundStatement prql_bind(ClientContext &context, Binder &binder,
                         OperatorExtensionInfo *info, SQLStatement &statement) {
  switch (statement.type) {
  case StatementType::EXTENSION_STATEMENT: {
    auto &extension_statement = dynamic_cast<ExtensionStatement &>(statement);
    if (extension_statement.extension.parse_function == prql_parse) {
      if (statement.query.empty()) {
        throw BinderException("PRQL statement source text not found");
      }
      std::string sql_query_or_error;
      if (!compile_prql(statement.query, sql_query_or_error)) {
        // sql_query_or_error contains error string
        throw ParserException(sql_query_or_error);
      }

      auto parser = Parser::GetBuiltinParser();
      parser.ParseQuery(sql_query_or_error);
      auto prql_state =
          make_shared_ptr<PrqlState>(std::move(parser.statements[0]));
      context.registered_state->Remove("prql");
      context.registered_state->Insert("prql", prql_state);

      auto prql_binder = Binder::CreateBinder(context, &binder);
      return prql_binder->Bind(*prql_state->statement);
    }
  }
  default:
    // No-op empty
    return {};
  }
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(prql, loader) { LoadInternal(loader); }
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif

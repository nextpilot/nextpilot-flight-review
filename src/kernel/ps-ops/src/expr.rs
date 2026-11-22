//! 数学表达式求值：纯 Rust、零外部依赖、递归下降。
//!
//! 用途：数据源衍生 / 提取前预处理 / 曲线轴变换的数值表达式，
//! 例：`5*x + 10/sin(x)`、`x*-1`、`(x-1)/(x+1)`。
//!
//! 设计约束（来自用户澄清）：
//!   · 表达式在 Rust kernel 求值（datasource 侧的运算都在 Rust，高效）；
//!   · 单点求值 live 友好，批量只是逐点循环——有 kernel 缓存，无需强求向量化；
//!   · 编译一次成 AST，逐点复用，避免每点重新 parse。
//!
//! 支持：变量 x（值）、t（时间秒）；常量 pi/e；
//!       运算符 + - * / ^ 与一元负号；
//!       函数 sin/cos/tan/asin/acos/atan/abs/sqrt/ln/log10/log/exp/
//!            min/max/clamp/floor/ceil/round/sign/pow。

use std::collections::HashMap;

#[derive(Debug, Clone)]
enum Node {
    Num(f64),
    Var(char),              // 'x' 值, 't' 时间
    Unary(char, Box<Node>), // '-' / '+'
    Binary(char, Box<Node>, Box<Node>),
    Fn(String, Vec<Node>),
}

/// 编译表达式文本为可复用的求值器。
pub struct Expression {
    root: Node,
}

impl Expression {
    pub fn compile(text: &str) -> Result<Self, String> {
        let mut p = Parser {
            tokens: tokenize(text)?,
            pos: 0,
        };
        let root = p.parse_sum()?;
        if p.pos != p.tokens.len() {
            return Err(format!("表达式末尾有多余内容: {:?}", p.tokens[p.pos]));
        }
        Ok(Expression { root })
    }

    /// 单点求值：x 为当前值，t 为当前时间（秒）。
    pub fn eval(&self, x: f64, t: f64) -> f64 {
        let mut vars: HashMap<char, f64> = HashMap::new();
        vars.insert('x', x);
        vars.insert('t', t);
        eval_node(&self.root, &vars)
    }
}

fn eval_node(n: &Node, vars: &HashMap<char, f64>) -> f64 {
    match n {
        Node::Num(v) => *v,
        Node::Var(c) => *vars.get(c).unwrap_or(&0.0),
        Node::Unary(op, a) => {
            let v = eval_node(a, vars);
            match op {
                '-' => -v,
                _ => v,
            }
        }
        Node::Binary(op, a, b) => {
            let av = eval_node(a, vars);
            let bv = eval_node(b, vars);
            match op {
                '+' => av + bv,
                '-' => av - bv,
                '*' => av * bv,
                '/' => {
                    if bv.abs() < 1e-15 {
                        f64::NAN
                    } else {
                        av / bv
                    }
                }
                '^' => av.powf(bv),
                _ => f64::NAN,
            }
        }
        Node::Fn(name, args) => {
            let a: Vec<f64> = args.iter().map(|n| eval_node(n, vars)).collect();
            call_fn(name, &a)
        }
    }
}

fn call_fn(name: &str, a: &[f64]) -> f64 {
    let first = || a.first().copied().unwrap_or(0.0);
    match name {
        "sin" => first().sin(),
        "cos" => first().cos(),
        "tan" => first().tan(),
        "asin" => first().asin(),
        "acos" => first().acos(),
        "atan" => first().atan(),
        "abs" => first().abs(),
        "sqrt" => first().sqrt(),
        "ln" | "log" => first().ln(),
        "log10" => first().log10(),
        "exp" => first().exp(),
        "floor" => first().floor(),
        "ceil" => first().ceil(),
        "round" => first().round(),
        "sign" => {
            let v = first();
            if v > 0.0 { 1.0 } else if v < 0.0 { -1.0 } else { 0.0 }
        }
        "min" => a.iter().copied().fold(f64::INFINITY, f64::min),
        "max" => a.iter().copied().fold(f64::NEG_INFINITY, f64::max),
        "clamp" => {
            if a.len() < 3 {
                f64::NAN
            } else {
                a[0].max(a[1]).min(a[2])
            }
        }
        "pow" => {
            if a.len() < 2 { f64::NAN } else { a[0].powf(a[1]) }
        }
        _ => f64::NAN, // 未知函数 -> NaN，由上层透明传播（不崩内核）
    }
}

#[derive(Debug, Clone, PartialEq)]
enum Tok {
    Num(f64),
    Ident(String),
    Op(char),
    LParen,
    RParen,
    Comma,
}

fn tokenize(text: &str) -> Result<Vec<Tok>, String> {
    let bytes = text.as_bytes();
    let mut out = Vec::new();
    let mut i = 0;
    while i < bytes.len() {
        let c = bytes[i] as char;
        if c.is_whitespace() {
            i += 1;
            continue;
        }
        if c.is_ascii_digit() || c == '.' {
            let start = i;
            while i < bytes.len() {
                let d = bytes[i] as char;
                if d.is_ascii_digit() || d == '.' || d == 'e' || d == 'E'
                    || ((d == '+' || d == '-') && i > start
                        && (bytes[i - 1] as char == 'e' || bytes[i - 1] as char == 'E'))
                {
                    i += 1;
                } else {
                    break;
                }
            }
            let s = std::str::from_utf8(&bytes[start..i]).unwrap();
            let v: f64 = s.parse().map_err(|_| format!("非法数字: {s}"))?;
            out.push(Tok::Num(v));
            continue;
        }
        if c.is_ascii_alphabetic() || c == '_' {
            let start = i;
            while i < bytes.len() {
                let d = bytes[i] as char;
                if d.is_ascii_alphanumeric() || d == '_' {
                    i += 1;
                } else {
                    break;
                }
            }
            let s = std::str::from_utf8(&bytes[start..i]).unwrap().to_string();
            out.push(Tok::Ident(s));
            continue;
        }
        match c {
            '(' => { out.push(Tok::LParen); i += 1; }
            ')' => { out.push(Tok::RParen); i += 1; }
            ',' => { out.push(Tok::Comma); i += 1; }
            '+' | '-' | '*' | '/' | '^' => { out.push(Tok::Op(c)); i += 1; }
            _ => return Err(format!("非法字符: {c}")),
        }
    }
    Ok(out)
}

struct Parser {
    tokens: Vec<Tok>,
    pos: usize,
}

impl Parser {
    fn peek(&self) -> Option<&Tok> { self.tokens.get(self.pos) }

    fn parse_sum(&mut self) -> Result<Node, String> {
        let mut left = self.parse_product()?;
        while matches!(self.peek(), Some(Tok::Op('+')) | Some(Tok::Op('-'))) {
            let op = match self.tokens[self.pos] { Tok::Op(o) => o, _ => unreachable!() };
            self.pos += 1;
            let right = self.parse_product()?;
            left = Node::Binary(op, Box::new(left), Box::new(right));
        }
        Ok(left)
    }

    fn parse_product(&mut self) -> Result<Node, String> {
        let mut left = self.parse_unary()?;
        while matches!(self.peek(), Some(Tok::Op('*')) | Some(Tok::Op('/'))) {
            let op = match self.tokens[self.pos] { Tok::Op(o) => o, _ => unreachable!() };
            self.pos += 1;
            let right = self.parse_unary()?;
            left = Node::Binary(op, Box::new(left), Box::new(right));
        }
        Ok(left)
    }

    // 幂右结合：a^b^c = a^(b^c)。优先级高于一元负号（数学约定 -x^2 = -(x^2)）。
    fn parse_power(&mut self) -> Result<Node, String> {
        let base = self.parse_primary()?;
        if matches!(self.peek(), Some(Tok::Op('^'))) {
            self.pos += 1;
            // 指数位置允许负号（2^-3），故走 parse_unary；这也保证右结合。
            let exp = self.parse_unary()?;
            Ok(Node::Binary('^', Box::new(base), Box::new(exp)))
        } else {
            Ok(base)
        }
    }

    // 一元负号优先级低于幂：-x^2 = -(x^2)，要 (-x)^2 请写括号。
    fn parse_unary(&mut self) -> Result<Node, String> {
        if matches!(self.peek(), Some(Tok::Op('-')) | Some(Tok::Op('+'))) {
            let op = match self.tokens[self.pos] { Tok::Op(o) => o, _ => unreachable!() };
            self.pos += 1;
            let v = self.parse_unary()?;
            Ok(Node::Unary(op, Box::new(v)))
        } else {
            self.parse_power()
        }
    }

    fn parse_primary(&mut self) -> Result<Node, String> {
        match self.peek().cloned() {
            Some(Tok::Num(v)) => { self.pos += 1; Ok(Node::Num(v)) }
            Some(Tok::Ident(name)) => {
                self.pos += 1;
                // 常量
                if name == "pi" { return Ok(Node::Num(std::f64::consts::PI)); }
                if name == "e" { return Ok(Node::Num(std::f64::consts::E)); }
                // 单字符变量
                if name.len() == 1 {
                    let c = name.as_bytes()[0] as char;
                    if c == 'x' || c == 't' { return Ok(Node::Var(c)); }
                }
                // 函数调用
                if matches!(self.peek(), Some(Tok::LParen)) {
                    self.pos += 1;
                    let mut args = Vec::new();
                    if !matches!(self.peek(), Some(Tok::RParen)) {
                        loop {
                            args.push(self.parse_sum()?);
                            if matches!(self.peek(), Some(Tok::Comma)) {
                                self.pos += 1;
                            } else { break; }
                        }
                    }
                    if !matches!(self.peek(), Some(Tok::RParen)) {
                        return Err("函数缺少右括号".into());
                    }
                    self.pos += 1;
                    return Ok(Node::Fn(name, args));
                }
                Err(format!("未知标识: {name}"))
            }
            Some(Tok::LParen) => {
                self.pos += 1;
                let v = self.parse_sum()?;
                if !matches!(self.peek(), Some(Tok::RParen)) {
                    return Err("缺少右括号".into());
                }
                self.pos += 1;
                Ok(v)
            }
            other => Err(format!("表达式语法错误: {other:?}")),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ev(expr: &str, x: f64, t: f64) -> f64 {
        Expression::compile(expr).unwrap().eval(x, t)
    }

    #[test]
    fn arithmetic() {
        assert!((ev("5*x + 10/sin(x)", 1.0, 0.0) - (5.0 + 10.0 / 1.0_f64.sin())).abs() < 1e-9);
        assert!((ev("x*-1", 3.0, 0.0) + 3.0).abs() < 1e-9);
        assert!((ev("(x-1)/(x+1)", 2.0, 0.0) - (1.0 / 3.0)).abs() < 1e-9);
    }

    #[test]
    fn power_and_unary() {
        assert!((ev("x^2", 4.0, 0.0) - 16.0).abs() < 1e-9);
        assert!((ev("-x^2", 4.0, 0.0) + 16.0).abs() < 1e-9);
        assert!((ev("2^x", 3.0, 0.0) - 8.0).abs() < 1e-9);
    }

    #[test]
    fn functions_and_time() {
        assert!((ev("clamp(x,0,1)", 2.0, 0.0) - 1.0).abs() < 1e-9);
        assert!((ev("t + x", 2.0, 3.0) - 5.0).abs() < 1e-9);
        assert!((ev("sqrt(abs(x))", -9.0, 0.0) - 3.0).abs() < 1e-9);
    }
}
